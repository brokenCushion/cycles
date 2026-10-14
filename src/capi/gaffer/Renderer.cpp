/* SPDX-License-Identifier: Apache-2.0
 * Experimental Gaffer host adapter. The only Cycles include is the public C API.
 * Deliberately limited to static triangle meshes, one camera, simple surface
 * networks and point lights. Unsupported requests fail instead of approximating.
 */
#include "cycles_c.h"
#include "GafferScene/Private/IECoreScenePreview/Renderer.h"
#include "IECoreScene/MeshAlgo.h"
#include "IECoreScene/MeshPrimitive.h"
#include "IECoreScene/ShaderNetwork.h"
#include "IECore/SimpleTypedData.h"
#include "IECore/VectorTypedData.h"
#include <OpenImageIO/imageio.h>
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace {
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreScenePreview;
struct Api {
  HMODULE library;
  template<class T> T get(const char *name) const {
    auto p = GetProcAddress(library, name);
    if (!p) throw std::runtime_error(std::string("Missing C API export: ")+name);
    return reinterpret_cast<T>(p);
  }
  Api() {
    const char *path = std::getenv("CYCLES_C_LIBRARY");
    if (!path || !std::filesystem::path(path).is_absolute())
      throw std::runtime_error("CYCLES_C_LIBRARY must be absolute");
    library = LoadLibraryExW(std::filesystem::path(path).c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) throw std::runtime_error("Cannot load Cycles C front library");
  }
};
Api &api() { static Api value; return value; } // Retained until process exit by API contract.
#define C(name) api().get<decltype(&ccl_##name)>("ccl_" #name)
void ok(ccl_status status) {
  if (status != CCL_SUCCESS) throw std::runtime_error(C(last_error)());
}
template<class T> typename T::ValueType value(const CompoundDataMap &m, const char *name,
                                            typename T::ValueType fallback) {
  auto i = m.find(name);
  if (i == m.end()) return fallback;
  auto d = runTimeCast<const T>(i->second.get());
  if (!d) throw std::runtime_error(std::string("Wrong parameter type: ")+name);
  return d->readable();
}
ccl_transform transform(const Imath::M44f &m) {
  ccl_transform t{};
  for (int r=0;r<3;++r) for (int c=0;c<4;++c) t.m[r*4+c]=m[c][r];
  return t;
}
void set(ccl_node *node, const char *name, const Data *data) {
  if (auto d=runTimeCast<const FloatData>(data)) ok(C(node_set_float)(node,name,d->readable()));
  else if (auto d=runTimeCast<const IntData>(data)) ok(C(node_set_int)(node,name,d->readable()));
  else if (auto d=runTimeCast<const BoolData>(data)) ok(C(node_set_bool)(node,name,d->readable()));
  else if (auto d=runTimeCast<const Color3fData>(data)) {
    auto v=d->readable(); ok(C(node_set_float3)(node,name,{v.x,v.y,v.z}));
  } else if (auto d=runTimeCast<const V3fData>(data)) {
    auto v=d->readable(); ok(C(node_set_float3)(node,name,{v.x,v.y,v.z}));
  } else if (auto d=runTimeCast<const StringData>(data)) ok(C(node_set_string)(node,name,d->readable().c_str()));
  else throw std::runtime_error(std::string("Unsupported parameter: ")+name);
}

class CyclesC : public Renderer {
  struct Attributes : AttributesInterface {
    ConstCompoundObjectPtr data;
    explicit Attributes(const CompoundObject *d) : data(d->copy()) {}
  };
  struct Handle : ObjectInterface {
    CyclesC *owner;
    ccl_node *node, *geometry;
    bool camera;
    Handle(CyclesC *o,ccl_node *n,ccl_node *g=nullptr,bool c=false):owner(o),node(n),geometry(g),camera(c){}
    void transform(const TransformSamples &samples,const SampleTimes &) override {
      if (samples.size()!=1) throw std::runtime_error("Motion transforms outside proof-of-concept");
      std::lock_guard lock(owner->mutex);
      auto m=samples[0];
      if (camera) m=Imath::M44f().setScale(Imath::V3f(1,-1,-1))*m;
      ok(C(node_set_transform)(node,camera?"matrix":"tfm",::transform(m)));
      ok(C(node_tag_update)(node));
    }
    bool attributes(const AttributesInterface *a) override {
      if (!geometry) return false;
      std::lock_guard lock(owner->mutex);
      auto shader=owner->shader(static_cast<const Attributes *>(a));
      ok(C(node_set_array)(geometry,"used_shaders",CCL_SOCKET_NODE_ARRAY,&shader,1));
      ok(C(node_tag_update)(geometry)); return true;
    }
    void link(const InternedString &,const ConstObjectSetPtr &objects) override {
      if (objects) throw std::runtime_error("Light linking outside proof-of-concept");
    }
    void assignID(uint32_t id) override { ok(C(node_set_int)(node,"pass_id",int32_t(id))); }
    void assignInstanceID(uint32_t) override { throw std::runtime_error("Instance IDs outside proof-of-concept"); }
  };
  RenderType kind;
  std::recursive_mutex mutex;
  std::mutex pixelsMutex;
  ccl_session *session=nullptr;
  ccl_scene *scene=nullptr;
  CompoundDataMap options;
  struct OutputBuffer {
    std::string filename;
    ccl_node *pass=nullptr;
    int channels=0;
    std::vector<float> pixels;
  };
  std::map<std::string,OutputBuffer> outputs;
  int width=64,height=64;
  bool started=false;
  ccl_node *node(const char *type,const char *name) {
    ccl_node *n=nullptr; ok(C(node_create)(scene,type,name,&n));
    if(std::string(type)=="object")
      ok(C(node_set_float)(n,"shadow_terminator_geometry_offset",0));
    return n;
  }
  ccl_node *singleton(const char *type) {
    ccl_node *n=nullptr; ok(C(scene_singleton)(scene,type,&n)); return n;
  }
  void acquire() {
    if (session) return;
    ccl_init_params init{sizeof(init),std::getenv("CYCLES_C_RESOURCES"),std::getenv("TEMP")};
    ok(C(init)(&init));
    ccl_session_params p{}; p.size=sizeof(p); ok(C(session_params_default)(&p));
    p.background=kind!=Interactive; p.headless=kind!=Interactive; p.bvh_static=kind!=Interactive;
    p.threads=value<IntData>(options,"cycles:session:threads",4);
    p.use_osl=value<StringData>(options,"cycles:shadingsystem","OSL")=="OSL";
    auto device=value<StringData>(options,"cycles:device","CPU");
    if (device!="CPU") {
      uint32_t count=0; ok(C(device_count)(&count)); bool found=false;
      for(uint32_t i=0;i<count;++i) {
        ccl_device_desc d{}; d.size=sizeof(d); ok(C(device_info)(i,&d));
        if (device==d.backend) {p.device_index=i; found=true; break;}
      }
      if(!found) throw std::runtime_error("Requested device unavailable");
    }
    ok(C(session_create)(&p,&session)); ok(C(session_scene)(session,&scene));
    ok(C(node_set_bool)(singleton("background"),"transparent",1));
  }
  ccl_node *shader(const Attributes *a) {
    auto i=a->data->members().find("cycles:surface");
    if(i==a->data->members().end()) throw std::runtime_error("Explicit cycles:surface required");
    auto network=runTimeCast<const ShaderNetwork>(i->second.get());
    if(!network) throw std::runtime_error("Expected surface shader network");
    ccl_shader_graph *graph=nullptr; ok(C(shader_graph_create)(scene,&graph));
    std::map<InternedString,ccl_node *> nodes;
    for(const auto &[name,s] : network->shaders()) {
      ccl_node *n=nullptr; ok(C(shader_graph_add)(graph,s->getName().c_str(),name.c_str(),&n));
      for(const auto &[key,v]:s->parameters()) set(n,key.c_str(),v.get());
      nodes[name]=n;
    }
    for(const auto &[name,s]:network->shaders())
      for(const auto &c:network->inputConnections(name))
        ok(C(shader_graph_connect)(graph,nodes.at(c.source.shader),c.source.name.c_str(),
                                    nodes.at(c.destination.shader),c.destination.name.c_str()));
    ccl_node *out=nullptr; ok(C(shader_graph_output)(graph,&out));
    auto endpoint=network->getOutput();
    std::string outputName=endpoint.name.string();
    if(outputName.empty()) {
      ccl_socket_desc socket{}; socket.size=sizeof(socket);
      ok(C(node_socket_info)(nodes.at(endpoint.shader),CCL_SOCKET_OUTPUT,0,&socket));
      outputName=socket.name;
    }
    ok(C(shader_graph_connect)(graph,nodes.at(endpoint.shader),outputName.c_str(),out,"surface"));
    auto result=node("shader","surface"); ok(C(shader_set_graph)(result,graph)); return result;
  }
  static int32_t tile(const ccl_render_tile *t,void *user) noexcept {
    try {
      auto self=static_cast<CyclesC *>(user); std::lock_guard lock(self->pixelsMutex);
      auto &buffer=self->outputs.at(t->pass_name);
      if(t->channels!=buffer.channels) return 1;
      for(int y=0;y<t->height;++y)
        std::copy_n(t->pixels+y*t->width*t->channels,t->width*t->channels,
                    buffer.pixels.begin()+((t->y+y)*self->width+t->x)*t->channels);
      return 0;
    } catch(...) {return 1;}
  }
 public:
  static TypeDescription<CyclesC> description;
  CyclesC(RenderType type,const std::string &,const MessageHandlerPtr &) :kind(type) {
    if(type==SceneDescription) throw std::runtime_error("Scene serialization outside proof-of-concept");
  }
  ~CyclesC() override {if(session) C(session_destroy)(session);}
  InternedString name() const override {return "CyclesC";}
  void option(const InternedString &name,const Object *v) override {
    std::lock_guard lock(mutex);
    if(v) options[name]=runTimeCast<Data>(v->copy()); else options.erase(name);
  }
  void output(const InternedString &,const Output *o) override {
    std::lock_guard lock(mutex);
    if(!o || o->getType()!="exr") throw std::runtime_error("EXR output required");
    const auto &name=o->getData();
    if(name!="rgba" && name!="sample_count" && name!="normal" && name!="depth")
      throw std::runtime_error("Unsupported proof-of-concept pass");
    auto &buffer=outputs[name]; buffer.filename=o->getName();
    buffer.channels=name=="rgba"?4:name=="normal"?3:1;
  }
  AttributesInterfacePtr attributes(const CompoundObject *a) override {return new Attributes(a);}
  ObjectInterfacePtr camera(const std::string &,const CameraSamples &samples,const SampleTimes &,
                            const AttributesInterface *) override {
    std::lock_guard lock(mutex); acquire();
    if(samples.size()!=1) throw std::runtime_error("Camera motion unsupported");
    const auto &c=*samples[0]; auto n=singleton("camera");
    auto res=c.renderResolution(); width=res.x; height=res.y;
    auto f=c.frustum(); auto clip=c.getClippingPlanes();
    ok(C(node_set_enum)(n,"camera_type",c.getProjection()=="orthographic"?1:0));
    ok(C(node_set_float)(n,"fov",float(1.5707963267948966)));
    ok(C(node_set_int)(n,"full_width",width)); ok(C(node_set_int)(n,"full_height",height));
    ok(C(node_set_float)(n,"viewplane.left",f.min.x)); ok(C(node_set_float)(n,"viewplane.right",f.max.x));
    ok(C(node_set_float)(n,"viewplane.bottom",-f.max.y)); ok(C(node_set_float)(n,"viewplane.top",-f.min.y));
    ok(C(node_set_float)(n,"nearclip",clip.x)); ok(C(node_set_float)(n,"farclip",clip.y));
    ok(C(node_set_float)(n,"aperture_ratio",c.getPixelAspectRatio()));
    return new Handle(this,n,nullptr,true);
  }
  ObjectInterfacePtr object(const std::string &name,const ObjectSamples &samples,const SampleTimes &,
                            const AttributesInterface *a) override {
    std::lock_guard lock(mutex); acquire();
    if(samples.size()!=1) throw std::runtime_error("Geometry motion unsupported");
    auto input=runTimeCast<const MeshPrimitive>(samples[0].get());
    if(!input || input->interpolation()!="linear") throw std::runtime_error("Only linear meshes supported");
    auto mesh=MeshAlgo::triangulate(input);
    const auto &positions=mesh->variableData<V3fVectorData>("P")->readable();
    const auto &indices=mesh->vertexIds()->readable();
    auto geometry=node("mesh",name.c_str()); ok(C(mesh_resize)(geometry,positions.size(),indices.size()/3));
    std::vector<ccl_float3> p; for(auto v:positions) p.push_back({v.x,v.y,v.z});
    ccl_attribute_desc position{}; position.size=sizeof(position);
    position.standard="POSITION"; position.type=CCL_ATTRIBUTE_POINT; position.element=CCL_ATTRIBUTE_VERTEX;
    ok(C(geometry_set_attribute)(geometry,&position,0,p.data(),p.size()));
    ok(C(node_set_array)(geometry,"triangles",CCL_SOCKET_INT_ARRAY,indices.data(),indices.size()));
    std::vector<int32_t> zero(indices.size()/3,0);
    ok(C(node_set_array)(geometry,"shader",CCL_SOCKET_INT_ARRAY,zero.data(),zero.size()));
    auto normalIt=mesh->variables.find("N");
    if(normalIt!=mesh->variables.end()) {
      if(normalIt->second.interpolation!=PrimitiveVariable::Vertex || normalIt->second.indices)
        throw std::runtime_error("Proof-of-concept supports unindexed vertex normals");
      const auto *normals=mesh->variableData<V3fVectorData>("N");
      if(!normals) throw std::runtime_error("Expected float3 vertex normals");
      std::vector<ccl_float3> values;
      for(auto v:normals->readable()) values.push_back({v.x,v.y,v.z});
      ccl_attribute_desc normal{}; normal.size=sizeof(normal); normal.standard="VERTEX_NORMAL";
      normal.type=CCL_ATTRIBUTE_NORMAL; normal.element=CCL_ATTRIBUTE_VERTEX_NORMAL;
      ok(C(geometry_set_attribute)(geometry,&normal,0,values.data(),values.size()));
      std::fill(zero.begin(),zero.end(),1);
    }
    ok(C(node_set_array)(geometry,"smooth",CCL_SOCKET_BOOLEAN_ARRAY,zero.data(),zero.size()));
    auto material=shader(static_cast<const Attributes *>(a));
    ok(C(node_set_array)(geometry,"used_shaders",CCL_SOCKET_NODE_ARRAY,&material,1));
    auto object=node("object",name.c_str()); ok(C(node_set_node)(object,"geometry",geometry));
    return new Handle(this,object,geometry);
  }
  ObjectInterfacePtr light(const std::string &name,const ObjectSamples &,const SampleTimes &,
                           const AttributesInterface *a) override {
    std::lock_guard lock(mutex); acquire();
    auto data=static_cast<const Attributes *>(a)->data;
    auto network=runTimeCast<const ShaderNetwork>(data->members().at("cycles:light").get());
    if(!network || network->outputShader()->getName()!="point_light")
      throw std::runtime_error("Only constant point lights supported");
    const auto &params=network->outputShader()->parameters();
    auto n=node("pointlight",name.c_str());
    Imath::Color3f color(1);
    color*=value<FloatData>(params,"intensity",1);
    color*=value<Color3fData>(params,"color",Imath::Color3f(1));
    color*=powf(2.f,value<FloatData>(params,"exposure",0));
    color*=M_PI;
    ok(C(node_set_float3)(n,"strength",{color.x,color.y,color.z}));
    ok(C(node_set_float)(n,"radius",value<FloatData>(params,"size",0)));
    ccl_shader_graph *graph=nullptr; ok(C(shader_graph_create)(scene,&graph));
    ccl_node *emission=nullptr,*out=nullptr;
    ok(C(shader_graph_add)(graph,"emission","emission",&emission));
    ok(C(node_set_float3)(emission,"color",{1,1,1}));
    ok(C(node_set_float)(emission,"strength",1));
    ok(C(shader_graph_output)(graph,&out));
    ok(C(shader_graph_connect)(graph,emission,"emission",out,"surface"));
    auto material=node("shader","light"); ok(C(shader_set_graph)(material,graph));
    ok(C(node_set_array)(n,"used_shaders",CCL_SOCKET_NODE_ARRAY,&material,1));
    auto object=node("object",name.c_str()); ok(C(node_set_node)(object,"geometry",n));
    ok(C(node_set_uint64)(object,"light_set_membership",1));
    ok(C(node_set_uint64)(object,"shadow_set_membership",1));
    return new Handle(this,object);
  }
  ObjectInterfacePtr lightFilter(const std::string &,const ObjectSamples &,const SampleTimes &,
                                 const AttributesInterface *) override {throw std::runtime_error("Light filters unsupported");}
  void render() override {
    std::lock_guard lock(mutex); acquire();
    if(outputs.empty()) throw std::runtime_error("Output required");
    if(started) ok(C(session_cancel)(session));
    auto integrator=singleton("integrator");
    ok(C(node_set_int)(integrator,"seed",value<IntData>(options,"cycles:integrator:seed",1)));
    ok(C(node_set_bool)(integrator,"use_denoise",0));
    ok(C(node_set_bool)(integrator,"motion_blur",1));
    ok(C(node_set_enum)(integrator,"sampling_pattern",kind==Interactive?3:2));
    std::vector<ccl_output_pass> requested;
    for(auto &[name,buffer]:outputs) {
      if(!buffer.pass) {
        buffer.pass=node("pass",name.c_str());
        uint32_t count=0; ok(C(node_socket_count)(buffer.pass,CCL_SOCKET_INPUT,&count));
        bool found=false;
        for(uint32_t i=0;i<count;++i) {
          ccl_socket_desc socket{}; socket.size=sizeof(socket);
          ok(C(node_socket_info)(buffer.pass,CCL_SOCKET_INPUT,i,&socket));
          if(std::string(socket.name)!="type") continue;
          for(uint32_t j=0;j<socket.enum_count;++j) {
            ccl_enum_value value{}; value.size=sizeof(value);
            ok(C(socket_enum_value)("pass",CCL_SOCKET_INPUT,i,j,&value));
            if(std::string(value.name)==(name=="rgba"?"combined":name)) {
              ok(C(node_set_enum)(buffer.pass,"type",value.value)); found=true; break;
            }
          }
        }
        if(!found) throw std::runtime_error("Required pass type unavailable");
        ok(C(node_set_string)(buffer.pass,"name",name.c_str()));
      }
      buffer.pixels.assign(size_t(width)*height*buffer.channels,0);
      requested.push_back({sizeof(ccl_output_pass),name.c_str(),buffer.channels});
    }
    ok(C(session_set_output_callback)(session,requested.data(),uint32_t(requested.size()),tile,this));
    ccl_buffer_params buffer{}; buffer.size=sizeof(buffer);
    buffer.width=buffer.full_width=width; buffer.height=buffer.full_height=height;
    buffer.samples=value<IntData>(options,"cycles:session:samples",16);
    buffer.temp_directory=std::getenv("TEMP");
    ok(C(session_reset)(session,&buffer));
    // Reset intentionally preserves pause state. Interactive render() resumes
    // the job explicitly after an edit; start() alone would remain paused.
    if(kind==Interactive) ok(C(session_pause)(session,0));
    else ok(C(session_start)(session));
    started=true;
    if(kind!=Interactive) {ok(C(session_wait)(session)); save();}
  }
  void pause() override {std::lock_guard lock(mutex); if(started) ok(C(session_pause)(session,1));}
  DataPtr command(const InternedString name,const CompoundDataMap &) override {
    if(name=="capi:save") {std::lock_guard lock(pixelsMutex); save(); return new BoolData(true);}
    if(name=="capi:progress") {ccl_progress p{}; p.size=sizeof(p); ok(C(session_progress)(session,&p)); return new FloatData(float(p.fraction));}
    throw std::runtime_error("Unsupported command");
  }
  void save() {
    for(const auto &[name,buffer]:outputs) {
      auto out=OIIO::ImageOutput::create(buffer.filename);
      OIIO::ImageSpec spec(width,height,buffer.channels,OIIO::TypeDesc::FLOAT);
      if(!out || !out->open(buffer.filename,spec) || !out->write_image(OIIO::TypeDesc::FLOAT,buffer.pixels.data()) || !out->close())
        throw std::runtime_error("EXR output failed");
    }
  }
};
Renderer::TypeDescription<CyclesC> CyclesC::description("CyclesC");
}
