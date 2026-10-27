#!/usr/bin/env python3
"""Private MIC Gossip28 words / Gossip40 gain. Shared fonts stay unchanged."""
import argparse
from pathlib import Path
import generate_filter_type as donor
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'platform/3ds/mic_type_data.h'
def generate():
    previous=donor.WORDS,donor.CHARS
    try:
        donor.WORDS={28:('mic','gain','open','mute','off','80','160','silent')}
        donor.CHARS='0123456789.'
        return donor.generate().replace('generate_filter_type.py','generate_mic_type.py').replace('Seance Filter','Seance Mic').replace('::filter_type','::mic_type')
    finally:donor.WORDS,donor.CHARS=previous
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');args=p.parse_args();text=generate()
    if args.check:assert OUT.read_text()==text
    else:
        assert not OUT.exists(),'preserve existing generated masks'
        OUT.write_text(text)
    print('mic type: native masks '+('verified' if args.check else 'generated'))
