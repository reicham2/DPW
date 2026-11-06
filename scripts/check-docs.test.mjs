import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { cpSync, mkdtempSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, relative, resolve } from 'node:path';
import { test } from 'node:test';
import { checkDocs, DOC_FILES, ROOT, SOURCE_FILES } from './check-docs.mjs';

function fixture(t) {
	const root = mkdtempSync(resolve(tmpdir(), 'dpw-docs-'));
	t.after(() => rmSync(root, { recursive: true, force: true }));
	function copy(file) {
		const source = resolve(ROOT, file);
		const target = resolve(root, file);
		if (statSync(source).isDirectory()) mkdirSync(target, { recursive: true });
		else {
			mkdirSync(dirname(target), { recursive: true });
			cpSync(source, target);
		}
	}
	for (const file of [...SOURCE_FILES, ...DOC_FILES]) copy(file);
	// Copy only public source files referenced by managed docs, never the checkout or a real .env.
	for (const file of DOC_FILES) {
		const source = readFileSync(resolve(ROOT, file), 'utf8');
		for (const match of source.matchAll(/\[[^\]\n]+\]\(([^)\s]+)\)/g)) {
			if (/^(?:[a-z][a-z0-9+.-]*:|#|\/\/)/i.test(match[1])) continue;
			const target = relative(ROOT, resolve(dirname(resolve(ROOT, file)), match[1].split('#')[0]));
			copy(target);
		}
		for (const match of source.matchAll(/\b(?:node|bash|sh)\s+(?:--[\w-]+\s+)*((?:scripts|backend|frontend)\/[\w./-]+)/g)) {
			copy(match[1]);
		}
	}
	return root;
}

function replace(root, file, from, to) {
	const path = resolve(root, file);
	const source = readFileSync(path, 'utf8');
	assert.ok(source.includes(from), `Fixture does not contain ${from}`);
	writeFileSync(path, source.replace(from, to));
}

test('the real repository satisfies the documentation contract', () => {
	assert.doesNotThrow(() => checkDocs());
});

test('a published port change requires a semantic reference update; regeneration is idempotent', t => {
	const root = fixture(t);
	replace(root, 'docker-compose.yml', '"8000:80"', '"8001:80"');
	assert.throws(() => checkDocs(root), /repository reference is stale/);
	checkDocs(root, { write: true });
	const readme = readFileSync(resolve(root, 'README.md'), 'utf8');
	assert.match(readme, /http:\/\/localhost:8001/);
	checkDocs(root, { write: true });
	assert.equal(readFileSync(resolve(root, 'README.md'), 'utf8'), readme);
	assert.doesNotThrow(() => checkDocs(root));
});

test('a changed audit retention default is detected without any README edit', t => {
	const root = fixture(t);
	replace(root, 'backend/src/infra/app_config.cpp',
		'"DPW_AUDIT_RETENTION_DAYS", "14"', '"DPW_AUDIT_RETENTION_DAYS", "21"');
	assert.throws(() => checkDocs(root), /repository reference is stale/);
});

test('the production env_file typo cannot silently drop required configuration', t => {
	const root = fixture(t);
	writeFileSync(resolve(root, '.env'), 'DPW_CONFIG_ENCRYPTION_KEY=documentation-test-key\n');
	writeFileSync(resolve(root, 'stack.env'), '');
	const config = () => JSON.parse(execFileSync('docker', [
		'compose', '--env-file', '.env.example', '-f', 'docker-compose.yml',
		'config', '--format', 'json',
	], { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }));
	assert.equal(config().services.backend.environment.DPW_CONFIG_ENCRYPTION_KEY, 'documentation-test-key');
	assert.equal(config().services.frontend.environment.DPW_CONFIG_ENCRYPTION_KEY, 'documentation-test-key');
	replace(root, 'docker-compose.yml', 'path: ./.env', 'path: ./ .env');
	assert.equal(config().services.backend.environment.DPW_CONFIG_ENCRYPTION_KEY, undefined);
	assert.throws(() => checkDocs(root), /must load \.env as documented/);
});

test('regeneration alone does not accept a stale development URL example', t => {
	const root = fixture(t);
	replace(root, 'docker-compose-dev.yml', '"8000:80"', '"8001:80"');
	assert.throws(() => checkDocs(root, { write: true }), /DPW_PUBLIC_URL example must use frontend port 8001/);
	replace(root, 'README.md', 'DPW_PUBLIC_URL=http://localhost:8000', 'DPW_PUBLIC_URL=http://localhost:8001');
	assert.doesNotThrow(() => checkDocs(root, { write: true }));
});

test('supported runtime settings must appear in the ENV example', t => {
	const root = fixture(t);
	replace(root, '.env.example', '# DPW_AUDIT_RETENTION_DAYS=14', '');
	assert.throws(() => checkDocs(root), /\.env\.example is missing supported setting DPW_AUDIT_RETENTION_DAYS/);
});

test('broken links and missing heading anchors are rejected', t => {
	const root = fixture(t);
	replace(root, 'README.md', '# DPW', '[missing](docs/not-here.md)\n\n# DPW');
	assert.throws(() => checkDocs(root), /broken repository link docs\/not-here\.md/);
	replace(root, 'README.md', '[missing](docs/not-here.md)', '[missing](docs/operations.md#not-a-heading)');
	assert.throws(() => checkDocs(root), /missing heading in link/);
	replace(root, 'README.md', '[missing](docs/operations.md#not-a-heading)',
		'[missing](README.md#not-a-heading)\n\n```bash\n# Not a heading\n```');
	assert.throws(() => checkDocs(root), /missing heading in link/);
});

test('script references with flags are checked and prose is not treated as a command', t => {
	const root = fixture(t);
	replace(root, 'README.md', '# DPW', '# DPW\n\nDo not make a cosmetic edit.\n\n`node --test scripts/missing.mjs`');
	assert.throws(() => checkDocs(root), /command references missing script scripts\/missing\.mjs/);
	replace(root, 'README.md', '`node --test scripts/missing.mjs`', '');
	assert.doesNotThrow(() => checkDocs(root));
});

test('nonexistent Make and npm commands fail even after a cosmetic README edit', t => {
	const root = fixture(t);
	replace(root, 'README.md', '# DPW', '# DPW\n\nRun `make nonexistent-target`.');
	assert.throws(() => checkDocs(root), /Make target does not exist: nonexistent-target/);
	replace(root, 'README.md', '`make nonexistent-target`', '`npm run nonexistent-script`');
	assert.throws(() => checkDocs(root), /npm script does not exist: nonexistent-script/);
});

test('removing generated markers cannot bypass the contract', t => {
	const root = fixture(t);
	replace(root, 'README.md', '<!-- repository-reference:start -->', '');
	assert.throws(() => checkDocs(root, { write: true }), /exactly one ordered/);
});

test('unrelated implementation changes do not require touching documentation', t => {
	const root = fixture(t);
	writeFileSync(resolve(root, 'backend/src/unrelated.cpp'), '// An implementation-only change.\n');
	assert.doesNotThrow(() => checkDocs(root));
});
