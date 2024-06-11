import os
import subprocess
import numpy as np
from prettytable import PrettyTable

def get_command_mapper(config):
    return {
        '500.perlbench_r': [
            f'../run_peak_refrate_{config}.0000/perlbench_r_peak.{config} -I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1',
            f'../run_peak_refrate_{config}.0000/perlbench_r_peak.{config} -I./lib diffmail.pl 4 800 10 17 19 300',
            f'../run_peak_refrate_{config}.0000/perlbench_r_peak.{config} -I./lib splitmail.pl 6400 12 26 16 100 0',
        ],
        '502.gcc_r': [
            f'../run_peak_refrate_{config}.0000/cpugcc_r_peak.{config} gcc-pp.c -O3 -finline-limit=0 -fif-conversion -fif-conversion2 -o gcc-pp.opts-O3_-finline-limit_0_-fif-conversion_-fif-conversion2.s',
            f'../run_peak_refrate_{config}.0000/cpugcc_r_peak.{config} gcc-pp.c -O2 -finline-limit=36000 -fpic -o gcc-pp.opts-O2_-finline-limit_36000_-fpic.s',
            f'../run_peak_refrate_{config}.0000/cpugcc_r_peak.{config} gcc-smaller.c -O3 -fipa-pta -o gcc-smaller.opts-O3_-fipa-pta.s',
            f'../run_peak_refrate_{config}.0000/cpugcc_r_peak.{config} ref32.c -O5 -o ref32.opts-O5.s',
            f'../run_peak_refrate_{config}.0000/cpugcc_r_peak.{config} ref32.c -O3 -fselective-scheduling -fselective-scheduling2 -o ref32.opts-O3_-fselective-scheduling_-fselective-scheduling2.s',
        ],
        '505.mcf_r': [
            f'../run_peak_refrate_{config}.0000/mcf_r_peak.{config} inp.in',
        ],
        '520.omnetpp_r': [
            f'../run_peak_refrate_{config}.0000/omnetpp_r_peak.{config} -c General -r 0',
        ],
        '523.xalancbmk_r': [
            f'../run_peak_refrate_{config}.0000/cpuxalan_r_peak.{config} -v t5.xml xalanc.xsl',
        ],
        '525.x264_r': [
            f'../run_peak_refrate_{config}.0000/x264_r_peak.{config} --pass 1 --stats x264_stats.log --bitrate 1000 --frames 1000 -o BuckBunny_New.264 BuckBunny.yuv 1280x720',
            f'../run_peak_refrate_{config}.0000/x264_r_peak.{config} --pass 2 --stats x264_stats.log --bitrate 1000 --dumpyuv 200 --frames 1000 -o BuckBunny_New.264 BuckBunny.yuv 1280x720',
            f'../run_peak_refrate_{config}.0000/x264_r_peak.{config} --seek 500 --dumpyuv 200 --frames 1250 -o BuckBunny_New.264 BuckBunny.yuv 1280x720',
        ],
        '531.deepsjeng_r': [
            f'../run_peak_refrate_{config}.0000/deepsjeng_r_peak.{config} ref.txt',
        ],
        '541.leela_r': [
            f'../run_peak_refrate_{config}.0000/leela_r_peak.{config} ref.sgf',
        ],
        '557.xz_r': [
            f'../run_peak_refrate_{config}.0000/xz_r_peak.{config} cld.tar.xz 160 19cf30ae51eddcbefda78dd06014b4b96281456e078ca7c13e1c0c9e6aaea8dff3efb4ad6b0456697718cede6bd5454852652806a657bb56e07d61128434b474 59796407 61004416 6',
            f'../run_peak_refrate_{config}.0000/xz_r_peak.{config} cpu2006docs.tar.xz 250 055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae 23047774 23513385 6e',
            f'../run_peak_refrate_{config}.0000/xz_r_peak.{config} input.combined.xz 250 a841f68f38572a49d86226b7ff5baeb31bd19dc637a922a972b2e6d1257a890f6a544ecab967c313e370478c74f760eb229d4eef8a8d2836d233d3e9dd1430bf 40401484 41217675 7',
        ],
        '508.namd_r': [
            f'../run_peak_refrate_{config}.0000/namd_r_peak.{config} --input apoa1.input --output apoa1.ref.output --iterations 65',
        ],
        '510.parest_r': [
            f'../run_peak_refrate_{config}.0000/parest_r_peak.{config} ref.prm',
        ],
        '511.povray_r': [
            f'../run_peak_refrate_{config}.0000/povray_r_peak.{config} SPEC-benchmark-ref.ini',
        ],
        '519.lbm_r': [
            f'../run_peak_refrate_{config}.0000/lbm_r_peak.{config} 3000 reference.dat 0 0 100_100_130_ldc.of',
        ],
        '526.blender_r': [
            f'../run_peak_refrate_{config}.0000/blender_r_peak.{config} sh3_no_char.blend --render-output sh3_no_char_ --threads 1 -b -F RAWTGA -s 849 -e 849 -a',
        ],
        '538.imagick_r': [
            f'../run_peak_refrate_{config}.0000/imagick_r_peak.{config} -limit disk 0 refrate_input.tga -edge 41 -resample 181% -emboss 31 -colorspace YUV -mean-shift 19x19+15% -resize 30% refrate_output.tga',
        ],
        '544.nab_r': [
            f'../run_peak_refrate_{config}.0000/nab_r_peak.{config} 1am0 1122214447 122',
        ],
    }

def safe_run_command(command, cwd):
    while True:
        output = subprocess.check_output(
            [command], shell=True, stderr=subprocess.DEVNULL, cwd=cwd)
        lines = output.strip().splitlines()[-3:]
        time = int(lines[0].split()[1].decode())
        memory = int(lines[1].split()[1].decode())
        retval = int(lines[2].split()[1].decode())
        if retval == 0 and time > 1000: ## HACK: Avoid any potential errors
            return time, memory

SPECTEST = os.path.join(os.path.dirname(os.path.realpath(__file__)), 'spectest')

table = PrettyTable()
native_command_mapper = get_command_mapper('native')
shadowbound_command_mapper = get_command_mapper('shadowbound')

table.field_names = ['Benchmark', 'Time', 'Memory']
time_overheads = []
memory_overheads = []

for bench in get_command_mapper('').keys():
    print(f'Testing {bench}')
    native_time, native_memory = 0, 0
    for idx, command in enumerate(native_command_mapper[bench]):
        print(f'[native] Running "{command}"')
        time, memory = safe_run_command(
            f"{SPECTEST} {command}",
            f"/root/cpu2017/benchspec/CPU/{bench}/run/run_peak_refrate_native.0000"
        )
        print('Time: ', time, 'ms')
        print('Memory: ', memory, 'KB')
        
        native_time += time
        native_memory += memory
        
    shadowbound_time, shadowbound_memory = 0, 0
    for idx, command in enumerate(shadowbound_command_mapper[bench]):
        print(f'[shadowbound] Running "{command}"')
        time, memory = safe_run_command(
            f"{SPECTEST} {command}",
            f"/root/cpu2017/benchspec/CPU/{bench}/run/run_peak_refrate_shadowbound.0000"
        )

        print('Time: ', time, 'ms')
        print('Memory: ', memory, 'KB')
        
        shadowbound_time += time
        shadowbound_memory += memory
        
    time_overhead = shadowbound_time / native_time
    memory_overhead = shadowbound_memory / native_memory
    table.add_row([bench, f"{time_overhead:.2f}x", f"{memory_overhead:.2f}x"])
    
    time_overheads.append(max(time_overhead - 1, 1000 / native_time))
    memory_overheads.append(max(memory_overhead - 1, 1024 / native_memory))

geometric_mean_time_overhead = np.prod(time_overheads) ** (1 / len(time_overheads)) * 100
geometric_mean_memory_overhead = np.prod(memory_overheads) ** (1 / len(memory_overheads)) * 100
table.add_row(['Geometric Mean', f"{geometric_mean_time_overhead:.2f}%", f"{geometric_mean_memory_overhead:.2f}%"])

print(table)
