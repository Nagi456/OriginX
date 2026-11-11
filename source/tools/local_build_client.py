"""tools/build_client.py with the SDK root already laid out by local_sdk_from_git.py (no downloads).
Usage: local_build_client.py --game <GAME> --msvc-root <xwin splat> --compiler <clang-cl of Clang 19+> ..."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_client as B
B.prepare = lambda *a, **k: None
import argparse
p = argparse.ArgumentParser()
p.add_argument('--game', default=B.DEFAULT_GAME); p.add_argument('--sdk-root', type=Path, default=None)
p.add_argument('--msvc-root', type=Path); p.add_argument('--compiler', default='clang-cl-18'); p.add_argument('--linker', default='lld-link-18')
p.add_argument('--standalone', action='store_true')
p.add_argument('--readobj', default='llvm-readobj-18'); p.add_argument('--dlltool', default='llvm-dlltool-18')
B.build(p.parse_args())
