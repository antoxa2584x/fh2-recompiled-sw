# fh2 - names of their own for the functions every recompiled module defines ("nomes dos módulos").
#
# Codegen names the PowerPC register save/restore helpers (__savegprlr_N, __restgprlr_N, __savefpr_N,
# __restfpr_N, __savevmx_N, __restvmx_N) and the entry point (xstart) the same way in every module: default.xex
# and the DLL modules (XMediaFacade, SpeechFacade) all define them. As shared libraries (PC, Android) each
# module keeps its own. The Switch NRO links every module into one executable (dll_targets.cmake), where they
# would collide, and the Switch toolchain links with --allow-multiple-definition (NVK's Rust libraries), so the
# collision would not even fail: a DLL's entry point would silently run the game's xstart.
#
# This step prefixes those names with the module's folder in each DLL module's generated code
# (xmediafacade_default__savegprlr_14, speechfacade_default_xstart); default.xex keeps its names. It is
# idempotent and runs after every codegen (tools/codegen.sh).
#
# Usage: python3 tools/nomes_modulos.py [generated folder (default app/generated)]
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOMES = re.compile(r'(?<![A-Za-z0-9_])((?:__imp__)?)(__(?:save|rest)(?:gprlr|fpr|vmx)_\d+|xstart)(?![A-Za-z0-9_])')


def main():
    gerado = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, 'app', 'generated')
    total = 0
    for modulo in sorted(os.listdir(gerado)):
        pasta = os.path.join(gerado, modulo)
        # Only the DLL modules: the entry point module (default) keeps the names hooks and tools know.
        if modulo == 'default' or not os.path.isfile(os.path.join(pasta, 'sources.cmake')):
            continue
        trocas = 0
        for f in sorted(os.listdir(pasta)):
            if not f.endswith(('.cpp', '.h')):
                continue
            caminho = os.path.join(pasta, f)
            texto = open(caminho, encoding='utf-8').read()
            novo, n = NOMES.subn(lambda m: m.group(1) + modulo + '_' + m.group(2), texto)
            if n:
                open(caminho, 'w', encoding='utf-8', newline='\n').write(novo)
                trocas += n
        print('%s: %d nomes trocados' % (modulo, trocas))
        total += trocas
    return 0


if __name__ == '__main__':
    sys.exit(main())
