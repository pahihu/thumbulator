import ctypes as c
from pathlib import Path
from struct import unpack_from, pack_into
import sys

llvm = c.CDLL('libLLVM-19.so')
ptr, inp, num, out = c.c_void_p, c.c_char_p, c.c_int, c.POINTER(c.c_void_p)

def api(name, result, *args):
    fn = getattr(llvm, 'LLVM' + name)
    fn.argtypes = args
    fn.restype = result
    return fn

for name in ('TargetInfo','Target','TargetMC','AsmPrinter','AsmParser'):
    api('InitializeARM' + name, None)()

def check(status, message):
    if status:
        raise RuntimeError(c.cast(message, inp).value.decode())

asm = Path(sys.argv[1]).read_text()
triple = b'thumbv6m-none-eabi'
ir = b'target triple = "' + triple + b'"\n'
for line in asm.splitlines():
    encoded = line.replace('\\', r'\5C').replace('"', r'\22')
    ir += b'module asm "' + encoded.encode() + b'"\n'

memory = api('CreateMemoryBufferWithMemoryRangeCopy', ptr, inp, c.c_size_t, inp)(ir, len(ir), b'asm')
context = api('ContextCreate', ptr)()
module, message, target = ptr(), ptr(), ptr()
check(api('ParseIRInContext', num, ptr, ptr, out, out)(context, memory, c.byref(module), c.byref(message)), message)
check(api('GetTargetFromTriple', num, inp, out, out)(triple, c.byref(target), c.byref(message)), message)
machine = api('CreateTargetMachine', ptr, ptr, inp, inp, inp, num, num, num)(
    target, triple, b'cortex-m0', b'', 2, 0, 0)
check(api('TargetMachineEmitToFile', num, ptr, ptr, inp, num, out)(
    machine, module, b'fixture.o', 1, c.byref(message)), message)

elf = Path('fixture.o').read_bytes()
assert elf[:7] == b'\x7fELF\x01\x01\x01'
start = unpack_from('<I',elf,32)[0]
stride, count, name_index = unpack_from('<HHH',elf,46)
sections = [unpack_from('<10I',elf,start + stride*i) for i in range(count)]
def contents(section):
    return elf[section[4]:section[4] + section[5]]
names = contents(sections[name_index])
def name(section):
    return names[section[0]:].split(b'\0',1)[0]

index = [name(section) for section in sections].index(b'.text')
code = bytearray(contents(sections[index]))
for rel in sections:
    if rel[1] != 9 or rel[7] != index:
        continue
    symbols = contents(sections[rel[6]])
    for pos in range(0,rel[5],rel[9]):
        address, info = unpack_from('<II',contents(rel),pos)
        assert (info & 0xff) == 2
        _, value, _, _, _, origin = unpack_from('<IIIBBH',symbols,16*(info >> 8))
        assert origin == index
        addend = unpack_from('<I',code,address)[0]
        pack_into('<I',code,address,value + addend)

Path(sys.argv[2]).write_bytes(code)
