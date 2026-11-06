import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = fileURLToPath(new URL('../', import.meta.url));
export const DOC_FILES = [
	'README.md',
	'docs/operations.md',
	'AGENTS.md',
	'.github/copilot-instructions.md',
	'.github/pull_request_template.md',
];
export const SOURCE_FILES = [
	'.env.example',
	'stack.env',
	'Makefile',
	'docker-compose.yml',
	'docker-compose-dev.yml',
	'backend/CMakeLists.txt',
	'backend/Dockerfile',
	'backend/src/main.cpp',
	'backend/src/infra/app_config.cpp',
	'db/init.sql',
	'frontend/Dockerfile',
	'frontend/package.json',
];

const START = '<!-- repository-reference:start -->';
const END = '<!-- repository-reference:end -->';
const text = (root, file) => readFileSync(resolve(root, file), 'utf8').replace(/\r\n/g, '\n');

function requiredMatch(source, pattern, label) {
	const match = source.match(pattern);
	if (!match) throw new Error(`Cannot extract ${label}; update the documentation extractor for the source change.`);
	return match;
}

function compose(root, file) {
	// Do not read local secrets, interpolate shell overrides, start containers or require a daemon.
	const output = execFileSync('docker', [
		'compose', '--env-file', '.env.example', '-f', file, 'config',
		'--format', 'json', '--no-env-resolution', '--no-interpolate', '--no-path-resolution',
	], { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
	const config = JSON.parse(output);
	if (!config.services || !Object.keys(config.services).length) {
		throw new Error(`${file}: no Compose services found`);
	}
	for (const service of ['backend', 'frontend']) {
		const files = config.services[service]?.env_file ?? [];
		if (!files.some(entry => entry.path.replace(/^\.\//, '') === '.env')) {
			throw new Error(`${file}: ${service} must load .env as documented (check env_file spelling).`);
		}
		for (const entry of files) {
			if (entry.path.replace(/^\.\//, '') !== '.env' && !existsSync(resolve(root, entry.path))) {
				throw new Error(`${file}: missing env_file ${entry.path}`);
			}
		}
	}
	return config;
}

export function loadFacts(root = ROOT) {
	const settings = requiredMatch(text(root, 'backend/src/infra/app_config.cpp'),
		/static const std::vector<SettingDef> defs = \{([\s\S]*?)\n\s*\};/, 'app setting definitions')[1];
	const definitions = settings.trim().split('\n').map(line => {
		const match = requiredMatch(line.trim(),
			/^\{k\w+, "([A-Z0-9_]*)", "([^"]*)", (true|false), (true|false), ValueType::\w+, (\d+), (\d+)\},?$/,
			'app setting row');
		return { env: match[1], default: match[2], min: match[5], max: match[6] };
	});
	const template = new Set([...text(root, '.env.example').matchAll(/^\s*#?\s*([A-Z][A-Z0-9_]*)=/gm)]
		.map(match => match[1]));
	for (const env of ['DPW_CONFIG_ENCRYPTION_KEY', ...definitions.map(def => def.env).filter(Boolean)]) {
		if (!template.has(env)) throw new Error(`.env.example is missing supported setting ${env}`);
	}
	const retention = definitions.find(def => def.env === 'DPW_AUDIT_RETENTION_DAYS');
	if (!retention) throw new Error('Audit retention definition is missing; review its documentation.');
	const startup = text(root, 'backend/src/main.cpp');
	const jobs = [...startup.matchAll(/cron\.add\("([^"]+)", "[^"]+", (\d+),/g)]
		.map(match => ({ id: match[1], minutes: match[2] }));
	if (!jobs.length || jobs.length !== [...startup.matchAll(/\bcron\.add\s*\(/g)].length) {
		throw new Error('Cannot extract all cron jobs; update the documentation extractor.');
	}
	const reminder = requiredMatch(text(root, 'db/init.sql'),
		/activity_reminder_hours INTEGER NOT NULL DEFAULT (\d+) CHECK \(activity_reminder_hours IN \(([\d, ]+)\)\)/,
		'reminder lead times');
	const cpp = requiredMatch(text(root, 'backend/CMakeLists.txt'),
		/set\(CMAKE_CXX_STANDARD (\d+)\)/, 'backend language standard')[1];
	const frontendBases = [...text(root, 'frontend/Dockerfile').matchAll(/^FROM (\S+) AS (?:build|runtime)$/gm)]
		.map(match => match[1]);
	if (frontendBases.length !== 2) throw new Error('Cannot extract frontend build/runtime images.');
	const targets = new Set([...text(root, 'Makefile').matchAll(/^([a-zA-Z][\w-]*):(?!=)/gm)]
		.map(match => match[1]));
	return {
		production: compose(root, 'docker-compose.yml'),
		development: compose(root, 'docker-compose-dev.yml'),
		cpp, frontendBases, retention, jobs, reminderDefault: reminder[1],
		reminderChoices: reminder[2].split(',').map(value => value.trim()),
		targets, npmScripts: JSON.parse(text(root, 'frontend/package.json')).scripts,
	};
}

function ports(service) {
	return service?.ports?.map(port =>
		`\`${port.host_ip || '0.0.0.0'}:${port.published || '<random>'} -> ${port.target}/${port.protocol}\``)
		.join(', ') || 'not published';
}

export function renderReference(facts) {
	const services = [...new Set([
		...Object.keys(facts.production.services), ...Object.keys(facts.development.services),
	])].sort();
	const lines = [
		START,
		'<!-- Generated by node scripts/check-docs.mjs --write; do not edit by hand. -->',
		'',
		'| Service | Production image | Development image/build | Published production ports | Published development ports |',
		'| --- | --- | --- | --- | --- |',
	];
	for (const name of services) {
		const prod = facts.production.services[name];
		const dev = facts.development.services[name];
		const build = dev?.build;
		const devImage = build ? `${build.dockerfile} (context: ${build.context})` : dev?.image;
		lines.push(`| \`${name}\` | ${prod?.image ? `\`${prod.image}\`` : 'not included'} | ${devImage ? `\`${devImage}\`` : 'not included'} | ${ports(prod)} | ${ports(dev)} |`);
	}
	const frontendPort = facts.production.services.frontend.ports?.find(port => port.target === 80);
	if (!frontendPort?.published) throw new Error('No published frontend HTTP port; review the setup instructions.');
	lines.push(
		'',
		`Default browser URL: http://localhost:${frontendPort.published}.`,
		`Backend language: C++${facts.cpp}. Frontend build/runtime images: ${facts.frontendBases.map(value => `\`${value}\``).join(' / ')}.`,
		'',
		'| Service | Development build arguments (Compose defaults) |',
		'| --- | --- |',
		...['backend', 'frontend'].map(name => {
			const args = facts.development.services[name].build?.args;
			if (!args) throw new Error(`No development build arguments for ${name}; review the build instructions.`);
			const values = Object.entries(args).sort(([a], [b]) => a.localeCompare(b))
				.map(([key, value]) => `\`${key}=${value}\``).join(', ');
			return `| \`${name}\` | ${values} |`;
		}),
		'',
		'| Service | Production ENV files (in order) | Development ENV files (in order) |',
		'| --- | --- | --- |',
		...['backend', 'frontend'].map(name => {
			const envFiles = config => config.services[name].env_file.map(entry =>
				`\`${entry.path}\` (${entry.required ? 'required' : 'optional'})`).join(', ');
			return `| \`${name}\` | ${envFiles(facts.production)} | ${envFiles(facts.development)} |`;
		}),
		'',
		`Audit retention: default **${facts.retention.default} days**, allowed **${facts.retention.min}-${facts.retention.max} days**.`,
		`Activity reminder lead time: default **${facts.reminderDefault} hours**, choices **${facts.reminderChoices.join(', ')} hours**.`,
		'',
		'| Background job | Default interval (minutes) |',
		'| --- | --- |',
		...facts.jobs.map(job => `| \`${job.id}\` | ${job.minutes} |`),
		'',
		END,
	);
	return lines.join('\n');
}

function headings(source) {
	const seen = new Map();
	const anchors = new Set();
	const prose = source.replace(/^```[^\n]*\n[\s\S]*?^```\s*$/gm, '');
	for (const match of prose.matchAll(/^#{1,6}\s+(.+?)\s*#*\s*$/gm)) {
		const base = match[1].toLowerCase().replace(/<[^>]*>/g, '')
			.replace(/[^\p{L}\p{N}_\-\s]/gu, '').replace(/\s/g, '-');
		const count = seen.get(base) || 0;
		anchors.add(count ? `${base}-${count}` : base);
		seen.set(base, count + 1);
	}
	return anchors;
}

export function checkLinks(root, file, source) {
	const prose = source.replace(/^```[^\n]*\n[\s\S]*?^```\s*$/gm, '');
	for (const match of prose.matchAll(/\[[^\]\n]+\]\(([^)\s]+)\)/g)) {
		const href = match[1];
		if (/^(?:[a-z][a-z0-9+.-]*:|\/\/)/i.test(href)) continue;
		const [path, anchor] = href.split('#');
		const target = path ? resolve(dirname(resolve(root, file)), decodeURIComponent(path)) : resolve(root, file);
		const local = relative(root, target);
		if (local === '..' || local.startsWith(`..\\`) || local.startsWith('../') || !existsSync(target)) {
			throw new Error(`${file}: broken repository link ${href}`);
		}
		if (anchor && target.endsWith('.md') && !headings(text(root, local)).has(decodeURIComponent(anchor))) {
			throw new Error(`${file}: missing heading in link ${href}`);
		}
	}
}

function checkCommands(root, file, source, facts) {
	const commands = [...source.matchAll(/```[^\n]*\n([\s\S]*?)```|`([^`\n]+)`/g)]
		.map(match => match[1] ?? match[2]).join('\n');
	for (const match of commands.matchAll(/\bmake ([a-zA-Z][\w-]*)/g)) {
		if (!facts.targets.has(match[1])) throw new Error(`${file}: undocumented Make target does not exist: ${match[1]}`);
	}
	for (const match of commands.matchAll(/\bnpm (?:run ([\w:-]+)|(test))\b/g)) {
		const name = match[1] || match[2];
		if (!Object.hasOwn(facts.npmScripts, name)) throw new Error(`${file}: frontend npm script does not exist: ${name}`);
	}
	for (const match of commands.matchAll(/\b(?:node|bash|sh)\s+(?:--[\w-]+\s+)*((?:scripts|backend|frontend)\/[\w./-]+)/g)) {
		if (!existsSync(resolve(root, match[1]))) throw new Error(`${file}: command references missing script ${match[1]}`);
	}
	for (const match of commands.matchAll(/^DPW_PUBLIC_URL=http:\/\/localhost:(\d+)\s*$/gm)) {
		const port = facts.development.services.frontend.ports?.find(port => port.target === 80)?.published;
		if (match[1] !== port) throw new Error(`${file}: development DPW_PUBLIC_URL example must use frontend port ${port}`);
	}
}

export function checkDocs(root = ROOT, { write = false } = {}) {
	const facts = loadFacts(root);
	const reference = renderReference(facts);
	const rawReadme = readFileSync(resolve(root, 'README.md'), 'utf8');
	const normalized = rawReadme.replace(/\r\n/g, '\n');
	let readme = normalized;
	if (readme.split(START).length !== 2 || readme.split(END).length !== 2 ||
		readme.indexOf(START) >= readme.indexOf(END)) {
		throw new Error('README.md must contain exactly one ordered repository-reference marker pair.');
	}
	const existing = readme.slice(readme.indexOf(START), readme.indexOf(END) + END.length);
	if (existing !== reference && !write) {
		throw new Error('README.md repository reference is stale. Run node scripts/check-docs.mjs --write and review all affected prose.');
	}
	if (write) readme = readme.replace(existing, reference);
	for (const file of DOC_FILES) {
		const source = file === 'README.md' ? readme : text(root, file);
		checkLinks(root, file, source);
		checkCommands(root, file, source, facts);
	}
	if (write && readme !== normalized) {
		writeFileSync(resolve(root, 'README.md'), rawReadme.includes('\r\n') ? readme.replace(/\n/g, '\r\n') : readme);
	}
	return facts;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	try {
		if (process.argv.slice(2).some(arg => arg !== '--write')) throw new Error('Usage: node scripts/check-docs.mjs [--write]');
		checkDocs(ROOT, { write: process.argv.includes('--write') });
		console.log('Documentation contract passed: source-derived reference, ENV coverage, commands and local links.');
	} catch (error) {
		console.error(`Documentation contract failed: ${error.message}`);
		process.exitCode = 1;
	}
}
