import re
import os

with open('src/window_manager.c', 'r') as f:
    lines = f.readlines()

blocks = {
    'wm.c': [(344,372), (374,383), (385,423), (425,475), (947,984), (1285,1415)],
    'arrange.c': [(633,658), (739,817), (819,870), (872,882), (986,1014), (1016,1105), (1107,1129)],
    'workspace.c': [(15,21), (23,34), (87,91), (93,111), (113,117), (119,142), (144,149), (294,342), (532,552), (660,737), (1131,1146), (1148,1164), (1166,1183), (1185,1221), (1545,1574), (1576,1606)],
    'window.c': [(36,85), (151,163), (165,191), (193,251), (268,292), (477,515), (517,530), (1497,1543)],
    'events.c': [(884,945), (1223,1283), (1417,1495)],
    'debug.c': [(253,260), (262,266), (554,631)]
}

headers = """#include "window_manager.h"
#include "config.h"
#include "internal.h"
#include "layout.h"
#include "list.h"
#include "logger.h"
#include "memory.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

"""

exports = []

for out_file, ranges in blocks.items():
    content = headers
    for start, end in ranges:
        func_lines = lines[start-1:end]
        func_text = "".join(func_lines)
        
        if func_text.startswith("static"):
            func_text = re.sub(r'^static\s+', '', func_text, count=1)
            brace_idx = func_text.find('{')
            if brace_idx != -1:
                sig = func_text[:brace_idx].strip()
                sig = re.sub(r'\s+', ' ', sig)
                exports.append(sig + ";")
        
        content += func_text + "\n"
        
    with open('src/' + out_file, 'w') as f:
        f.write(content)

with open('src/internal.h', 'a') as f:
    f.write("\n// --- Internal Module Functions ---\n")
    for e in exports:
        f.write(e + "\n")

# Update CMakeLists.txt
with open('CMakeLists.txt', 'r') as f:
    cmake = f.read()

cmake = cmake.replace('src/window_manager.c', 
'''src/wm.c
    src/arrange.c
    src/workspace.c
    src/window.c
    src/events.c
    src/debug.c''')

with open('CMakeLists.txt', 'w') as f:
    f.write(cmake)

print("Split completion successful.")
