"""Retained rejection evidence skips rendering only if its recorded gate still passes."""
import tempfile
from pathlib import Path
from run_deep_regression import retained_rejection

def check():
    with tempfile.TemporaryDirectory() as name:
        d=Path(name);key='boundary-CPU-reject';result={'cases':{}}
        previous={'cases':{key:dict(passed=True,rejection=True)}}
        assert not retained_rejection(None,result,key,d)
        (d/'rejection.log').write_text('explicit capacity failure')
        (d/'scene.deep.exr').write_bytes(b'preserve')
        assert retained_rejection(previous,result,key,d,'capacity failure')
        assert result['cases'][key]==previous['cases'][key]
        (d/'scene.deep.exr').write_bytes(b'changed')
        try:retained_rejection(previous,result,key,d,'capacity failure')
        except ValueError:pass
        else:raise AssertionError('Changed output was reused')
        (d/'scene.deep.exr').write_bytes(b'preserve')
        (d/'scene.deep.exr.partial-1').touch()
        try:retained_rejection(previous,result,key,d)
        except ValueError:pass
        else:raise AssertionError('Partial publication was reused')
    print('PASS rejection resume: no rerender, saved gate evidence rechecked')

if __name__=='__main__':check()
