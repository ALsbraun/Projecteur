"""Exercise the actual trusted-workflow policy with mocked GitHub responses."""
import contextlib
import copy
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = yaml.safe_load((ROOT / '.github/workflows/dependabot-automerge.yml').read_text())
RUN = WORKFLOW['jobs']['enable']['steps'][0]['run']
CODE = RUN.split("python3 - <<'PY'\n", 1)[1].rsplit('\nPY', 1)[0]
OLD_SHA = 'a' * 40
NEW_SHA = 'b' * 40
HEAD = 'c' * 40
REPO = 'ALsbraun/Projecteur'


def change(old, new):
    return {
        'filename': '.github/workflows/fedora-rpm.yml', 'status': 'modified',
        'additions': 1, 'deletions': 1,
        'patch': f'@@ -1 +1 @@\n-{old}\n+{new}',
    }


class PolicyTest(unittest.TestCase):
    def allowed(self, file, modify=None):
        responses = {
            f'repos/{REPO}/pulls/7': {
                'state': 'open', 'head': {'sha': HEAD, 'repo': {'full_name': REPO},
                'ref': 'dependabot/github_actions/action-patches'},
                'base': {'ref': 'main'}, 'user': {'login': 'dependabot[bot]'}, 'changed_files': 1,
            },
            f'repos/{REPO}/commits/{HEAD}': {
                'author': {'login': 'dependabot[bot]'}, 'commit': {'verification': {'verified': True}},
            },
            f'repos/{REPO}/pulls/7/files?per_page=100': [file],
            'repos/actions/checkout/git/ref/tags/v7.0.2': {'object': {'type': 'commit', 'sha': NEW_SHA}},
            'repos/actions/upload-artifact/git/ref/tags/v7.0.2': {'object': {'type': 'commit', 'sha': NEW_SHA}},
        }
        if modify:
            modify(responses)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'output'
            env = {'GH_REPO': REPO, 'PR_NUMBER': '7', 'EXPECTED_HEAD': HEAD, 'GITHUB_OUTPUT': str(output)}
            with patch.dict(os.environ, env), patch('subprocess.check_output',
                    side_effect=lambda args: json.dumps(responses[args[-1]]).encode()), contextlib.redirect_stdout(io.StringIO()):
                try:
                    exec(compile(CODE, '<workflow policy>', 'exec'), {})
                except SystemExit as error:
                    self.assertEqual(error.code, 0)
            return output.exists() and output.read_text() == 'eligible=true\n'

    def checkout_patch(self):
        return change(f'      - uses: actions/checkout@{OLD_SHA} # v7.0.1',
                      f'      - uses: actions/checkout@{NEW_SHA} # v7.0.2')

    def test_real_list_item_patch(self):
        self.assertTrue(self.allowed(self.checkout_patch()))

    def test_real_named_step_digest(self):
        self.assertTrue(self.allowed(change(
            f'        uses: actions/upload-artifact@{OLD_SHA} # v7.0.2',
            f'        uses: actions/upload-artifact@{NEW_SHA} # v7.0.2')))

    def test_reject_new_major_minor_downgrade(self):
        for version in ('8.0.0', '7.1.0', '7.0.0'):
            with self.subTest(version=version):
                file = self.checkout_patch()
                file['patch'] = file['patch'].replace(f'{NEW_SHA} # v7.0.2', f'{NEW_SHA} # v{version}')
                self.assertFalse(self.allowed(file))

    def test_reject_arbitrary_code_or_permissions(self):
        for line in ('        run: echo injected', '      contents: write',
                     f'      - uses: unknown/action@{NEW_SHA} # v7.0.2'):
            with self.subTest(line=line):
                file = self.checkout_patch()
                file['patch'] += '\n+' + line
                file['additions'] += 1
                self.assertFalse(self.allowed(file))

    def test_reject_wrong_sha(self):
        file = self.checkout_patch()
        file['patch'] = file['patch'].replace(NEW_SHA, 'd' * 40)
        self.assertFalse(self.allowed(file))

    def test_reject_changed_head_fork_and_unverified_commit(self):
        mutations = (
            lambda r: r[f'repos/{REPO}/pulls/7']['head'].update(sha='d' * 40),
            lambda r: r[f'repos/{REPO}/pulls/7']['head']['repo'].update(full_name='someone/fork'),
            lambda r: r[f'repos/{REPO}/commits/{HEAD}']['commit']['verification'].update(verified=False),
        )
        for mutate in mutations:
            self.assertFalse(self.allowed(self.checkout_patch(), mutate))

    def test_reject_new_file_or_truncated_diff(self):
        for field, value in [('status', 'added'), ('filename', 'src/main.cc'), ('additions', 2)]:
            file = copy.deepcopy(self.checkout_patch())
            file[field] = value
            self.assertFalse(self.allowed(file))


if __name__ == '__main__':
    unittest.main()
