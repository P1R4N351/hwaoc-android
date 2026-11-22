"""The README's Support section and .github/FUNDING.yml name the same donation targets.

README.md must carry the 'Support with Bitcoin' badge linking to #support and a '## Support' heading; the Bitcoin
address shown there must be the one in FUNDING.yml; FUNDING.yml must list exactly the two targets below."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
README = ROOT / 'README.md'
FUNDING = ROOT / '.github/FUNDING.yml'

BITCOIN = 'bc1q4a3lqcg87rvw7vxulausdqhhuwkwwa6w6v4mxx'
TARGETS = ['bitcoin:' + BITCOIN, 'https://www.rinnegatamante.eu/vitadb/#/user/PiSCES']
BADGE = re.compile(r'^\[!\[Support with Bitcoin\]\([^)\s]+\)\]\(#support\)$', re.M)
ADDRESS = re.compile(r'\bbc1[02-9ac-hj-np-z]{11,71}\b')


def read(path):
    return path.read_text(encoding='utf-8')


def funding_targets(text):
    lines = [line for line in text.splitlines() if line.strip() and not line.lstrip().startswith('#')]
    assert len(lines) == 1 and lines[0].startswith('custom:'), lines
    body = lines[0][len('custom:'):].strip()
    assert body.startswith('[') and body.endswith(']'), body
    return re.findall(r"'([^']*)'", body)


class SupportLinks(unittest.TestCase):
    def test_readme_badge_and_heading(self):
        text = read(README)
        self.assertEqual(len(BADGE.findall(text)), 1, 'README.md: one Support with Bitcoin badge linking #support')
        self.assertEqual(re.findall(r'^## Support$', text, re.M), ['## Support'])

    def test_funding_lists_exactly_the_two_targets(self):
        self.assertEqual(funding_targets(read(FUNDING)), TARGETS)

    def test_readme_bitcoin_address_matches_funding(self):
        readme_addresses = set(ADDRESS.findall(read(README)))
        funding = [t for t in funding_targets(read(FUNDING)) if t.startswith('bitcoin:')]
        self.assertEqual(len(funding), 1)
        self.assertEqual(readme_addresses, {funding[0][len('bitcoin:'):]})


if __name__ == '__main__':
    unittest.main()
