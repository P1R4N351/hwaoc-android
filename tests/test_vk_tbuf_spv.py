"""vk_tbuf_spv.h: sampled-2D bindings the game feeds with buffer textures become texel buffers.

Needs a C compiler and SPIRV-Tools (spirv-as, spirv-val, spirv-dis); skipped without them. The fixture is
hand-written (games/totk/android/gfx/tbuf_test.spvasm): no game shader code."""
import shutil, subprocess, tempfile, unittest
from pathlib import Path

GFX = Path(__file__).resolve().parents[1] / 'games/totk/android/gfx'
ENV = 'vulkan1.1'


@unittest.skipUnless(all(shutil.which(x) for x in ('cc', 'spirv-as', 'spirv-val', 'spirv-dis')), 'needs C compiler and SPIRV-Tools')
class TbufRetypeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(); cls.path = Path(cls.temp.name)
        cls.probe = cls.path / 'probe'
        subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        '-I' + str(GFX), str(GFX / 'test_vk_tbuf_spv.c'), '-o', str(cls.probe)], check=True, capture_output=True, text=True)
        cls.text = (GFX / 'tbuf_test.spvasm').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def assemble(self, name, text):
        src, out = self.path / (name + '.spvasm'), self.path / (name + '.spv')
        src.write_text(text)
        subprocess.run(['spirv-as', '--target-env', ENV, str(src), '-o', str(out)], check=True, capture_output=True, text=True)
        subprocess.run(['spirv-val', '--target-env', ENV, str(out)], check=True, capture_output=True, text=True)
        return out

    def run_probe(self, source, *bindings):
        fixed = self.path / (source.stem + '.fixed.spv')
        if fixed.exists(): fixed.unlink()
        r = subprocess.run([str(self.probe), str(source), str(fixed)] + [str(b) for b in bindings], capture_output=True, text=True)
        return r, fixed

    def dis(self, path):
        return subprocess.run(['spirv-dis', str(path)], check=True, capture_output=True, text=True).stdout

    def test_one_of_two_bindings_is_retyped(self):
        source = self.assemble('base', self.text)
        r, fixed = self.run_probe(source, 1)
        self.assertEqual(r.returncode, 0, r.stderr); self.assertIn('result 1 ', r.stdout)
        subprocess.run(['spirv-val', '--target-env', ENV, str(fixed)], check=True, capture_output=True, text=True)
        text = self.dis(fixed)
        self.assertEqual(text.count(' Buffer 0 0 0 1 Unknown'), 1)       # one buffer image type
        self.assertEqual(text.count(' 2D 0 0 0 1 Unknown'), 1)           # the other binding keeps its 2D image
        self.assertEqual(text.count('OpCapability SampledBuffer'), 1)
        fetches = [l.split('OpImageFetch')[1].split() for l in text.splitlines() if 'OpImageFetch' in l]
        self.assertEqual(sorted(len(f) for f in fetches), [3, 5])        # buffer: type image index; 2D: + Lod 0
        index = next(f for f in fetches if len(f) == 3)[2]
        self.assertIn(f'{index} = OpConstant %int 7', text)              # the x component became the texel index

    def test_both_bindings_share_the_new_types(self):
        r, fixed = self.run_probe(self.assemble('both', self.text), 1, 2)
        self.assertEqual(r.returncode, 0, r.stderr); self.assertIn('result 2 ', r.stdout)
        subprocess.run(['spirv-val', '--target-env', ENV, str(fixed)], check=True, capture_output=True, text=True)
        text = self.dis(fixed)
        self.assertEqual(text.count(' Buffer 0 0 0 1 Unknown'), 1)
        self.assertEqual(text.count('OpTypeSampledImage'), 2)            # the unused 2D one stays declared
        self.assertTrue(all(len(l.split('OpImageFetch')[1].split()) == 3 for l in text.splitlines() if 'OpImageFetch' in l))

    def test_an_existing_buffer_type_is_reused(self):
        text = self.text.replace('%psimg = OpTypePointer UniformConstant %simg',
                                 '%psimg = OpTypePointer UniformConstant %simg\n %bimg = OpTypeImage %float Buffer 0 0 0 1 Unknown')
        text = text.replace('OpCapability Shader', 'OpCapability Shader\n OpCapability SampledBuffer')
        r, fixed = self.run_probe(self.assemble('reuse', text), 1)
        self.assertEqual(r.returncode, 0, r.stderr)
        subprocess.run(['spirv-val', '--target-env', ENV, str(fixed)], check=True, capture_output=True, text=True)
        out = self.dis(fixed)
        self.assertEqual(out.count(' Buffer 0 0 0 1 Unknown'), 1)        # a second declaration would be invalid
        self.assertEqual(out.count('OpCapability SampledBuffer'), 1)

    def test_a_binding_that_is_not_an_image_is_left_alone(self):
        r, fixed = self.run_probe(self.assemble('none', self.text), 0)
        self.assertEqual(r.returncode, 0, r.stderr); self.assertIn('result 0 ', r.stdout); self.assertFalse(fixed.exists())

    def refused(self, name, text, *bindings):
        r, fixed = self.run_probe(self.assemble(name, text), *bindings)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr); self.assertIn('result -1 ', r.stdout); self.assertFalse(fixed.exists())

    def test_refuses_when_only_some_bindings_match(self):
        self.refused('partial', self.text, 1, 0)

    def test_refuses_another_use_of_the_image(self):
        text = self.text.replace('OpCapability Shader', 'OpCapability Shader\n OpCapability ImageQuery')
        text = text.replace('%ca = OpCompositeConstruct %iv2 %c7 %c0', '%qs = OpImageQuerySizeLod %iv2 %ia %c0\n %ca = OpCompositeConstruct %iv2 %c7 %c0')
        self.refused('query', text, 1)

    def test_refuses_a_coordinate_that_is_not_a_two_component_construct(self):
        text = self.text.replace('%fa = OpImageFetch %v4 %ia %ca Lod %c0', '%cc = OpCopyObject %iv2 %ca\n %fa = OpImageFetch %v4 %ia %cc Lod %c0')
        self.refused('coord', text, 1)

    def test_refuses_a_retyped_value_passed_on(self):
        text = self.text.replace('%ia = OpImage %img %sa', '%sc = OpCopyObject %simg %sa\n %ia = OpImage %img %sc')
        self.refused('copy', text, 1)


if __name__ == '__main__':
    unittest.main()
