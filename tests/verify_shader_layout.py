"""Verify the compiled SPIR-V ABI against lighting.hpp's static assertions."""
import argparse
import re
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--spirv-dis', default='spirv-dis')
parser.add_argument('--shader-dir', type=Path, default=Path('shaders'))
args = parser.parse_args()
shader = args.shader_dir / 'dodecahedron.frag.spv'
assembly = subprocess.check_output([args.spirv_dis, str(shader)], text=True)

def struct_id(name):
    result = re.search(r'OpName\s+(%\w+)\s+"' + name + '"', assembly)
    assert result, f'Missing GPU struct: {name}'
    return result.group(1)

for name, offsets in {'Scene': [0, 64, 80, 96, 112, 128],
                      'Object': [0, 64, 128, 144]}.items():
    identifier = struct_id(name)
    for member, offset in enumerate(offsets):
        assert re.search(rf'OpMemberDecorate\s+{re.escape(identifier)}\s+{member}\s+Offset\s+{offset}\b', assembly), (name, member, offset)

for name, stride in [('PointLight', 32), ('SpotLight', 64)]:
    # GLSL may emit function-local and storage versions of the same named struct.
    types = re.findall(r'OpName\s+(%\w+)\s+"' + name + '"', assembly)
    found = False
    for identifier in types:
        array = re.search(r'(%\w+)\s+=\s+OpTypeRuntimeArray\s+' + re.escape(identifier) + r'\b', assembly)
        if array:
            assert re.search(r'OpDecorate\s+' + re.escape(array.group(1)) + rf'\s+ArrayStride\s+{stride}\b', assembly), name
            found = True
    assert found, f'{name} must use a runtime SSBO array'

for variable, descriptor_set, binding in [('scene', 0, 0), ('object', 1, 0), ('points', 0, 1), ('spots', 0, 2)]:
    identifier = struct_id(variable)
    for decoration, value in [('DescriptorSet', descriptor_set), ('Binding', binding)]:
        assert re.search(r'OpDecorate\s+' + re.escape(identifier) + rf'\s+{decoration}\s+{value}\b', assembly), variable
print('PASS: SPIR-V Scene/Object offsets; Point/Spot runtime-array strides; sets 0/1 and bindings')

for name, offsets in {'TexturePush': [0, 16, 32]}.items():
    identifier = struct_id(name)
    for member, offset in enumerate(offsets):
        assert re.search(rf'OpMemberDecorate\s+{re.escape(identifier)}\s+{member}\s+Offset\s+{offset}\b', assembly), (name, member, offset)
assert 'PushConstant' in assembly
for variable, binding in [('albedoMap', 1), ('specularMap', 2), ('emissiveMap', 3)]:
    identifier = struct_id(variable)
    assert re.search(r'OpDecorate\s+' + re.escape(identifier) + r'\s+DescriptorSet\s+1\b', assembly), variable
    assert re.search(r'OpDecorate\s+' + re.escape(identifier) + rf'\s+Binding\s+{binding}\b', assembly), variable
assert assembly.count('OpImageSampleImplicitLod') >= 4, 'Expected multiple albedo samples plus specular/emissive maps'
print('PASS: 48-byte push block, material sampler bindings, multiple compiled texture samples')
