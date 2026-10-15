"""Minimal host-only Cycles 5.3 source adaptation, guarded and reproducible."""
import sys
sys.dont_write_bytecode = True
from workspace_guard import output, disk, self_test
self_test()
disk()
root = output('D:/temp/cycles_c_api/phase9-20261013/gaffer/src/GafferCycles/IECoreCyclesPreview')

def edit(name, old, new):
    path = output(root/name)
    text = path.read_text()
    if new in text:
        return
    assert old in text, (name, old)
    path.write_text(text.replace(old, new))

# Light is abstract in 5.3. Create the appropriate concrete geometry, and let
# Gaffer's existing false-on-incompatible-edit protocol recreate changed types.
edit('Attributes.h', '\t\tvoid applyLight( ccl::Light *light, ccl::Scene *scene ) const;',
'''\t\tccl::Light *createLight( ccl::Scene *scene ) const;
\t\tint lightType() const;
\t\tvoid applyLight( ccl::Light *light, ccl::Scene *scene ) const;''')
edit('Attributes.cpp', 'void Attributes::applyLight( ccl::Light *light, ccl::Scene *scene ) const',
'''int Attributes::lightType() const
{
    const std::string name = m_lightAttribute && m_lightAttribute->outputShader()
        ? m_lightAttribute->outputShader()->getName() : "point_light";
    if( name == "spot_light" ) return ccl::LIGHT_SPOT;
    if( name == "distant_light" ) return ccl::LIGHT_SUN;
    if( name == "background_light" ) return ccl::LIGHT_BACKGROUND;
    if( name == "quad_light" || name == "disk_light" || name == "portal" ) return ccl::LIGHT_AREA;
    return ccl::LIGHT_POINT;
}

ccl::Light *Attributes::createLight( ccl::Scene *scene ) const
{
    switch( lightType() )
    {
        case ccl::LIGHT_SPOT : return SceneAlgo::createNodeWithLock<ccl::SpotLight>( scene );
        case ccl::LIGHT_SUN : return SceneAlgo::createNodeWithLock<ccl::SunLight>( scene );
        case ccl::LIGHT_BACKGROUND : return SceneAlgo::createNodeWithLock<ccl::BackgroundLight>( scene );
        case ccl::LIGHT_AREA : return SceneAlgo::createNodeWithLock<ccl::AreaLight>( scene );
        default : return SceneAlgo::createNodeWithLock<ccl::PointLight>( scene );
    }
}

void Attributes::applyLight( ccl::Light *light, ccl::Scene *scene ) const''')
edit('Renderer.cpp', 'CyclesLight( ccl::Scene *scene, const std::string &name, NodeDeleter *nodeDeleter )',
     'CyclesLight( ccl::Scene *scene, const std::string &name, NodeDeleter *nodeDeleter, const Attributes *attributes )')
edit('Renderer.cpp', 'm_light( SceneAlgo::createNodeWithLock<ccl::Light>( scene ),',
     'm_light( attributes->createLight( scene ),')
edit('Renderer.cpp', 'new CyclesLight( m_scene, name, m_nodeDeleter.get() )',
     'new CyclesLight( m_scene, name, m_nodeDeleter.get(), typedAttributes )')
edit('Renderer.cpp', '\t\t\tcyclesAttributes->applyLight( m_light.get(), m_scene );',
'''\t\t\tif( cyclesAttributes->lightType() != m_light->get_light_type() ) return false;
\t\t\tcyclesAttributes->applyLight( m_light.get(), m_scene );''')
edit('ShaderNetworkAlgo.cpp', 'ccl::LIGHT_DISTANT', 'ccl::LIGHT_SUN')
edit('ShaderNetworkAlgo.cpp', '\t\tcyclesLight->set_size( 1.0f );',
     '\t\t// Cycles 5.3 area dimensions no longer have a separate size multiplier.')
for method in ('sizeu', 'sizev', 'ellipse', 'spread'):
    edit('ShaderNetworkAlgo.cpp', 'cyclesLight->set_'+method+'(',
         'static_cast<ccl::AreaLight *>( cyclesLight )->set_'+method+'(')
edit('ShaderNetworkAlgo.cpp', 'cyclesLight->set_angle(', 'static_cast<ccl::SunLight *>( cyclesLight )->set_angle(')
edit('ShaderNetworkAlgo.cpp', 'cyclesLight->set_spot_angle(', 'static_cast<ccl::SpotLight *>( cyclesLight )->set_angle(')
edit('ShaderNetworkAlgo.cpp', '\t\t// Convert generic parameters.\n\t\telse',
'''\t\telse if( name == "size" )
        {
            SocketAlgo::setSocket( cyclesLight, "radius", value.get() );
        }
        else if( name == "spot_smooth" )
        {
            SocketAlgo::setSocket( cyclesLight, "smooth", value.get() );
        }
\t\t// Convert generic parameters.
\t\telse''')
print('Applied host-only concrete-light and renamed-light-socket adaptation')

# The light UI inventory used to find all sockets on the single Light type.
edit('IECoreCycles.cpp', '\t\tIECore::CompoundDataMap _in = _sockets->readable();',
'''\t\tIECore::CompoundDataMap _in = _sockets->readable();
        for( const char *name : { "pointlight", "spotlight", "arealight", "sunlight", "backgroundlight" } )
        {
            auto sockets = getSockets( ccl::NodeType::find( ccl::ustring( name ) ), false );
            _in.insert( sockets->readable().begin(), sockets->readable().end() );
        }
        _in["size"] = _in.at("radius");
        auto spotSockets = getSockets( ccl::NodeType::find( ccl::ustring("spotlight") ), false );
        _in["spot_angle"] = spotSockets->readable().at("angle");
        _in["spot_smooth"] = spotSockets->readable().at("smooth");''')
edit('IECoreCycles.cpp', '\t\t\tstd::string type( it->first.c_str() );\n\t\t\ttype += "_light";',
'''\t\t\tstd::string type( it->first.c_str() );
            if( type == "sun" ) type = "distant";
\t\t\ttype += "_light";''')

for filename in ('Attributes.cpp', 'Renderer.cpp', 'ShaderNetworkAlgo.cpp'):
    path = output(root/filename)
    text = path.read_text()
    for old, new in [('PATH_RAY_ALL_VISIBILITY','PATH_RAY_VISIBILITY_ALL')] + [
        ('PATH_RAY_'+s,'PATH_RAY_VISIBILITY_'+s)
        for s in ('CAMERA','DIFFUSE','GLOSSY','TRANSMIT','SHADOW','VOLUME_SCATTER')]:
        text = text.replace('ccl::'+old, 'ccl::'+new)
    path.write_text(text)

edit('MeshAlgo.cpp', 'cmesh->reserve_mesh( numVerts, numFaces );',
     'cmesh->resize_mesh( numVerts, mesh->interpolation() == "catmullClark" ? 0 : numFaces );')
edit('MeshAlgo.cpp', 'cmesh->add_vertex( ccl::make_float3( points[i].x, points[i].y, points[i].z ) );',
     'cmesh->attributes.find( ccl::ATTR_STD_POSITION )->data_for_write<ccl::packed_float3>()[i] = ccl::make_float3( points[i].x, points[i].y, points[i].z );')
edit('MeshAlgo.cpp', 'cmesh->reserve_subd_faces( numFaces, ncorners );',
     'cmesh->resize_subd_faces( numFaces, ncorners );\n        int ptexOffset = 0;')
edit('MeshAlgo.cpp', '''cmesh->add_subd_face(
\t\t\t\tconst_cast<int*>(&vertexIds[indexOffset]), vertsPerFace[i],
\t\t\t\t/* shader = */ 0, /* smooth = */ true
\t\t\t);''', '''cmesh->get_subd_start_corner()[i] = indexOffset;
            cmesh->get_subd_num_corners()[i] = vertsPerFace[i];
            cmesh->get_subd_shader()[i] = 0;
            cmesh->get_subd_smooth()[i] = true;
            cmesh->get_subd_ptex_offset()[i] = ptexOffset;
            ptexOffset += vertsPerFace[i] == 4 ? 1 : vertsPerFace[i];
            std::copy_n( &vertexIds[indexOffset], vertsPerFace[i], &cmesh->get_subd_face_corners()[indexOffset] );''')
edit('MeshAlgo.cpp', '''cmesh->add_triangle(
\t\t\t\tvertexIds[i], vertexIds[i+1], vertexIds[i+2],
\t\t\t\t/* shader = */ 0, /* smooth = */ smooth
\t\t\t);''', '''std::copy_n( &vertexIds[i], 3, &cmesh->get_triangles()[i] );
            cmesh->get_shader()[i / 3] = 0;
            cmesh->get_smooth()[i / 3] = smooth;''')
edit('MeshAlgo.cpp', 'std::copy_n( cmesh->get_verts().data(), cmesh->get_verts().size(), generated->data_float3() );',
'''std::copy_n( cmesh->attributes.find(ccl::ATTR_STD_POSITION)->data<ccl::packed_float3>(),
                     cmesh->num_verts(), generated->data_for_write<ccl::packed_float3>() );''')

edit('PointsAlgo.cpp', 'pointcloud->reserve( numPoints );',
'''pointcloud->resize( numPoints );
    auto setPoint = [&]( size_t i, ccl::float3 position, float radius ) {
        pointcloud->attributes.find(ccl::ATTR_STD_POSITION)->data_for_write<ccl::packed_float3>()[i] = position;
        pointcloud->attributes.find(ccl::ATTR_STD_RADIUS)->data_for_write<float>()[i] = radius;
        pointcloud->get_shader()[i] = 0;
    };''')
for radius in ('width[i] * 0.5f', 'width[i]', 'width'):
    edit('PointsAlgo.cpp', f'pointcloud->add_point( SocketAlgo::setVector( pos[i] ), {radius}, 0);',
         f'setPoint( i, SocketAlgo::setVector( pos[i] ), {radius} );')

edit('CurvesAlgo.cpp', 'hair->reserve_curves( numCurves, numKeys );',
'''hair->resize_curves( numCurves, numKeys );
    auto setKey = [&]( size_t key, ccl::float3 position, float radius ) {
        hair->attributes.find(ccl::ATTR_STD_POSITION)->data_for_write<ccl::packed_float3>()[key] = position;
        hair->attributes.find(ccl::ATTR_STD_RADIUS)->data_for_write<float>()[key] = radius;
    };''')
for radius in ('width[key] / 2.0f', 'constantWidth / 2.0f'):
    edit('CurvesAlgo.cpp', f'hair->add_curve_key( ccl::make_float3( points[key].x, points[key].y, points[key].z ), {radius} );',
         f'setKey( key, ccl::make_float3( points[key].x, points[key].y, points[key].z ), {radius} );')
edit('CurvesAlgo.cpp', 'hair->add_curve( firstKey, 0 );',
     'hair->get_curve_first_key()[i] = firstKey;\n            hair->get_curve_shader()[i] = 0;')

edit('GeometryAlgo.cpp', 'attribute->data_normal()', 'attribute->data_for_write<ccl::packed_normal>()')
edit('GeometryAlgo.cpp', 'ccl::float3 *f3 = attribute->data_float3();',
     'ccl::packed_float3 *f3 = attribute->data_for_write<ccl::packed_float3>();')
edit('GeometryAlgo.cpp', '(float *)attribute->data()', 'static_cast<float *>(attribute->data_for_write())')
edit('GeometryAlgo.cpp', 'attr->data_voxel() =', 'attr->data_voxel_for_write() =')
path = output(root/'GeometryAlgo.cpp')
text = path.read_text()
start = text.index('ccl::Attribute *positionAttribute =', text.index('void convertMotion('))
start = text.rfind('\n', 0, start) + 1
end = text.index('\n}\n\nvoid convertVoxelGrids', start)
text = text[:start] + '''    ccl::Attribute *positionAttribute = geometry.attributes.find( ccl::ATTR_STD_POSITION );
    positionAttribute->add_motion( &geometry );
    ccl::Attribute *radiusAttribute = geometry.attributes.find( ccl::ATTR_STD_RADIUS );
    if( radiusAttribute ) radiusAttribute->add_motion( &geometry );
    int step = 1;
    for( size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex )
    {
        if( sampleIndex == primarySampleIndex ) continue;
        const auto *pData = samples[sampleIndex]->variableData<V3fVectorData>( "P", PrimitiveVariable::Vertex );
        auto *dst = positionAttribute->data_for_write<ccl::packed_float3>(step);
        if( pData )
        {
            const auto &p = pData->readable();
            for( size_t i = 0; i < p.size(); ++i ) dst[i] = ccl::make_float3(p[i].x, p[i].y, p[i].z);
        }
        if( radiusAttribute ) std::copy_n( radiusAttribute->data<float>(), radiusAttribute->size,
                                          radiusAttribute->data_for_write<float>(step) );
        ++step;
    }''' + text[end:]
path.write_text(text)
print('Applied geometry attribute/storage and visibility-flag API adaptation')
edit('SocketAlgo.cpp', 'ccl::array<ccl::float3>', 'ccl::array<ccl::packed_float3>')
edit('Renderer.cpp', 'ccl::PathRayFlag> g_rayTypes', 'ccl::PathRayVisibilityFlag> g_rayTypes')
edit('GeometryAlgo.cpp', 'attribute->element_size( attributes.geometry, attributes.prim )',
     'attribute->element_size( attributes.geometry, attribute->element, attributes.prim )')
edit('SphereAlgo.cpp', '''pointcloud->reserve( 1 );
\tpointcloud->add_point( ccl::make_float3( 0.0f, 0.0f, 0.0f ), sphere->radius(), 0);''',
'''pointcloud->resize( 1 );
    pointcloud->attributes.find(ccl::ATTR_STD_POSITION)->data_for_write<ccl::packed_float3>()[0] = ccl::zero_float3();
    pointcloud->attributes.find(ccl::ATTR_STD_RADIUS)->data_for_write<float>()[0] = sphere->radius();
    pointcloud->get_shader()[0] = 0;''')
