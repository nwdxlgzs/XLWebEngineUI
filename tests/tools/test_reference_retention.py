import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import artifacts
import compare_text_reference as reference
import ci
from ci_support import cleanup_package_staging, cleanup_validation_scratch, host_identity, process_started_at, read_json, record_active, write_json


class ReferenceRetentionTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'build/test-tools'
        parent.mkdir(parents=True, exist_ok=True)
        directory = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(directory.cleanup)
        self.build = Path(directory.name)

    def report(self, index):
        path = self.build / 'reports/text-reference' / '20261103T000000-{:08x}'.format(index)
        write_json(path / 'report.json', dict(state='passed', browser='browser', library='library',
            source_digest='source', browser_sha256='a', library_sha256='b', font_sha256='c', cases={}))
        (path / 'profile').mkdir()
        (path / 'profile/cache').write_bytes(b'disposable browser cache')
        os.utime(path / 'report.json', (index, index))
        return path

    def test_migrates_old_reports_and_deletes_whole_profiles_beyond_five(self):
        paths = [self.report(i) for i in range(8)]
        earliest = read_json(paths[0] / 'report.json')
        for key in ('source_digest', 'browser_sha256', 'library_sha256'):
            earliest.pop(key)
        write_json(paths[0] / 'report.json', earliest)
        os.utime(paths[0] / 'report.json', (0, 0))
        reference.adopt_completed_reports(self.build)
        artifacts.collect(self.build, 5)
        self.assertEqual([p.exists() for p in paths], [False]*3+[True]*5)
        self.assertFalse((paths[0] / 'profile/cache').exists())
        self.assertTrue((paths[-1] / 'profile/cache').is_file())

    def test_live_report_is_kept_while_more_than_three_other_runs_complete(self):
        with reference.report_run(self.build) as active:
            for _ in range(7):
                with reference.report_run(self.build) as completed:
                    write_json(completed / 'report.json', {'state': 'passed'})
            self.assertTrue(active.exists())
            self.assertEqual(len(list(active.parent.iterdir())), 4)
            write_json(active / 'report.json', {'state': 'passed'})
        self.assertEqual(len(list(active.parent.iterdir())), 3)
        self.assertTrue(active.exists())

    def test_failure_and_interrupt_still_trim_to_three_and_release_lease(self):
        for error in (RuntimeError('failed comparison'), KeyboardInterrupt()):
            for _ in range(6):
                with self.assertRaises(type(error)):
                    with reference.report_run(self.build) as output:
                        raise error
            self.assertEqual(len(list((self.build / 'reports/text-reference').iterdir())), 3)
            self.assertFalse(artifacts.active_leases(self.build, output.relative_to(self.build).as_posix()))
            self.assertIn(read_json(output / 'report.json')['state'], ('failed', 'interrupted'))

    def test_does_not_adopt_unknown_or_running_directory(self):
        unknown = self.build / 'reports/text-reference/20261103T000000-abcdefab'
        write_json(unknown / 'report.json', {'state': 'running'})
        reference.adopt_completed_reports(self.build)
        self.assertFalse(artifacts.locations(self.build, unknown)[2].exists())

    def test_child_pid_lease_protects_profile_after_parent_lease_finishes(self):
        with reference.report_run(self.build) as output:
            child = subprocess.Popen([sys.executable, '-B', '-c', 'import time;time.sleep(20)'],
                creationflags=0x08000000 if os.name == 'nt' else 0)
            try:
                lease = artifacts.lease(self.build, output, owner_pid=child.pid)
                lease.__enter__()
                write_json(output / 'report.json', {'state': 'passed'})
            except BaseException:
                child.kill(); child.wait()
                raise
        try:
            for _ in range(7):
                with reference.report_run(self.build):
                    pass
            self.assertTrue(output.exists())
        finally:
            child.terminate(); child.wait(timeout=5)
            lease.__exit__(None, None, None)
        artifacts.collect(self.build)
        self.assertFalse(output.exists())

    def test_process_timeout_closes_child_and_lease(self):
        with reference.report_run(self.build) as output:
            with self.assertRaisesRegex(RuntimeError, 'timed out'):
                reference.process_output([sys.executable, '-B', '-c', 'import time;time.sleep(20)'],
                                         output, self.build, timeout=.1)
            leases = list((self.build / 'state/artifact-leases').rglob('*.json'))
            self.assertEqual(len(leases), 1)  # Only report_run owns a live lease.
            self.assertEqual(read_json(leases[0])['pid'], os.getpid())

    def test_scope_does_not_delete_unrelated_family(self):
        paths = []
        for i in range(7):
            path = self.build / 'cache' / str(i)
            path.mkdir(parents=True)
            artifacts.register(self.build, path, 'unrelated')
            paths.append(path)
        for _ in range(7):
            with reference.report_run(self.build):
                pass
        self.assertTrue(all(p.exists() for p in paths))

    def test_ad_hoc_logs_keep_five_per_target_and_delete_older_screenshots(self):
        for target in ('windows', 'musl'):
            for i in range(8):
                path = self.build / 'reports' / '{}-step-{}.log'.format(target, i)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('done')
                os.utime(path, (i, i))
        for i in range(7):
            path = self.build / 'reports' / 'demo-{}.png'.format(i)
            path.write_bytes(b'placeholder screenshot')
            os.utime(path, (i, i))
        artifacts.register_report_files(self.build)
        artifacts.collect(self.build, 5)
        artifacts.register_report_files(self.build)
        self.assertEqual(artifacts.collect(self.build, 5), [])
        for target in ('windows', 'musl'):
            self.assertEqual(len(list((self.build/'reports').glob(target+'-*.log'))), 5)
        self.assertEqual(len(list((self.build/'reports').glob('*.png'))), 5)

    def test_prune_only_dead_local_leases_and_pid_reuse(self):
        folder = self.build / 'state/artifact-leases/example'
        for name, record in {
            'dead': {'host': host_identity(), 'pid': 2147483647},
            'live': {'host': host_identity(), 'pid': os.getpid()},
            'reused': {'host': host_identity(), 'pid': os.getpid(), 'process_started_at': 1},
            'foreign': {'host': 'another-host', 'pid': 2147483647}
        }.items():
            write_json(folder / (name+'.json'), dict(record, state='running'))
        self.assertIsNotNone(process_started_at(os.getpid()))
        self.assertFalse(record_active(read_json(folder/'reused.json')))
        self.assertEqual(len(artifacts.prune_local_leases(self.build, dry_run=True)), 2)
        self.assertTrue((folder/'dead.json').exists())
        artifacts.prune_local_leases(self.build)
        self.assertEqual({p.stem for p in folder.glob('*.json')}, {'live', 'foreign'})

    def test_finished_packaging_drops_temporary_copies_and_keeps_archives(self):
        for state in ('passed', 'running'):
            run = self.build / 'runs/full' / state
            write_json(run / 'report.json', {'state': state, 'host': host_identity(), 'pid': os.getpid()})
            target = run / 'windows-x64'
            write_json(target / 'result.json', {'state': 'passed'})
            staged = target / 'packages/_CPack_Packages'
            staged.mkdir(parents=True)
            (staged / 'duplicate-library').write_bytes(b'large temporary library')
            (target / 'packages/sdk.zip').write_bytes(b'final package')
        removed = cleanup_package_staging(self.build, dry_run=True)
        self.assertEqual(len(removed), 1)
        self.assertTrue((self.build/'runs/full/passed/windows-x64/packages/_CPack_Packages').exists())
        cleanup_package_staging(self.build)
        self.assertFalse((self.build/'runs/full/passed/windows-x64/packages/_CPack_Packages').exists())
        self.assertTrue((self.build/'runs/full/passed/windows-x64/packages/sdk.zip').exists())
        self.assertTrue((self.build/'runs/full/running/windows-x64/packages/_CPack_Packages').exists())

    def test_forced_cleanup_ignores_recent_24h_stamp(self):
        write_json(self.build / 'state' / ('cleanup-'+ci.platform.system().lower()+'.json'), {'last_run': ci.time.time()})
        for i in range(8):
            path = self.build / 'reports' / 'windows-old-{}.log'.format(i)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('complete')
        with patch.object(ci, 'BUILD', self.build):
            result = ci.cleanup_completed_work()
        self.assertEqual(len(result['artifacts']), 5)
        self.assertEqual(len(list((self.build / 'reports').glob('*.log'))), 3)

    def test_validation_scratch_requires_audit_and_preserves_evidence(self):
        for name in ('complete', 'unaudited', 'active', 'foreign', 'empty'):
            run = self.build / 'runs/full' / name
            write_json(run / 'report.json', dict(mode='full', state='running' if name == 'active' else 'passed',
                       host=host_identity(), pid=os.getpid()))
            write_json(run / 'sdk-audit.json', {'state': 'failed' if name == 'unaudited' else 'passed'})
            target = run / 'windows-x64'
            write_json(target / 'result.json', dict(state='passed', host='other' if name == 'foreign' else host_identity()))
            (target / 'packages').mkdir()
            (target / 'packages/sdk.zip').write_bytes(b'final archive')
            if name == 'empty':
                continue
            log = target / 'Debug/consumer-build/Testing/Temporary/LastTest.log'
            log.parent.mkdir(parents=True)
            log.write_bytes(b'Test Passed.')
            (target / 'Debug/sdk-relocated').mkdir()
        candidates = cleanup_validation_scratch(self.build, dry_run=True)
        self.assertEqual(len(candidates), 2)
        removed = cleanup_validation_scratch(self.build)
        self.assertEqual(removed, candidates)
        target = self.build / 'runs/full/complete/windows-x64'
        self.assertEqual((target / 'Debug/evidence/consumer-tests.log').read_bytes(), b'Test Passed.')
        self.assertTrue((target / 'packages/sdk.zip').is_file())
        self.assertFalse((target / 'Debug/consumer-build').exists())
        self.assertNotIn('validation_scratch', read_json(self.build / 'runs/full/empty/windows-x64/result.json'))
        for name in ('unaudited', 'active', 'foreign'):
            self.assertTrue((self.build / 'runs/full' / name / 'windows-x64/Debug/consumer-build').is_dir())

    def test_gc_windows_entry_dispatches_wsl_verification(self):
        with patch.object(ci, 'BUILD', self.build), patch.object(ci.os, 'name', 'nt'), \
                patch.object(ci, 'capture', return_value='{"local":true}') as child, \
                patch.object(ci, 'linux_path', return_value='/workspace/tools/ci.py'), \
                patch.object(ci.shutil, 'which', return_value='wsl.exe'), \
                patch.object(ci, 'cleanup_completed_work', return_value={}), \
                patch.object(ci, 'cleanup_runs', return_value=[]), \
                patch.object(ci, 'WorkLock'), \
                patch.object(sys, 'argv', ['ci.py', 'gc', '--dry-run']):
            self.assertEqual(ci.main(), 0)
        args = child.call_args[0][0]
        self.assertIn('wsl.exe', args)
        self.assertIn('--local-only', args)
        self.assertIn('--dry-run', args)


if __name__ == '__main__':
    unittest.main()
