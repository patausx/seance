#!/usr/bin/env python3
"""Private GRAN native Gossip masks matching the approved wave-first study."""
import argparse
from pathlib import Path
import generate_filter_type as donor
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'platform/3ds/gran_type_data.h'
def generate():
    donor.WORDS={22:('flow','gate','burst','restart','hold'),28:('gran',)}
    donor.CHARS='0123456789.+-'
    return donor.generate().replace('generate_filter_type.py','generate_gran_type.py').replace('Seance Filter','Seance Gran').replace('::filter_type','::gran_type')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');args=p.parse_args();text=generate()
    if args.check:assert OUT.read_text()==text
    else:
        assert not OUT.exists(),'preserve generated files; back up before replacing'
        OUT.write_text(text)
    print('gran type: native masks verified' if args.check else 'gran type: native masks generated')
