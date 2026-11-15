"""C0 integration regressions; --baseline tests the exact stored pre-change source."""
from pathlib import Path
import argparse
import subprocess

p=argparse.ArgumentParser();p.add_argument('--baseline',action='store_true');args=p.parse_args()
root=Path(__file__).resolve().parents[1]
def read(name):
    if args.baseline:
        return subprocess.check_output(['git','-C',str(root),'show','9f64012:src/'+name],text=True,encoding='utf-8')
    return (root/'src'/name).read_text(encoding='utf-8')
checks={
    'default-off':('display.cpp','centered_ui_trace::configure(profile.enabled,profile.centered_hud,profile.trace)'),
    'successful-backbuffer-generation':('display.cpp','published=state; centered_ui_trace::applied(state);'),
    'frame-boundary':('display.cpp','centered_ui_trace::next_frame();'),
    'classic-enqueue':('classic_hud.cpp','centered_ui_trace::owner::classic_batch'),
    'private-font-render':('lc_fonts.cpp','centered_ui_trace::owner::menu_text'),
    'online-input':('online_menus.cpp','centered_ui_trace::owner::online'),
    'bxig-layout':('options_menu.cpp','centered_ui_trace::boundary::layout'),
    'bxig-render':('options_menu.cpp','centered_ui_trace::boundary::render'),
    'raw-pointer':('display.cpp','centered_ui_trace::owner::raw_pointer'),
}
failed=[]
for name,(file,needle) in checks.items():
    ok=needle in read(file)
    print(('PASS ' if ok else 'FAIL ')+name)
    if not ok:failed.append(name)
raise SystemExit(bool(failed))
