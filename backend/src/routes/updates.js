import fs from 'node:fs';
import { paths } from '../config.js';

export function requireApiKey(req, res, next) {
	const key = req.get('X-Whitegram-Key');
	if (!key || key !== req.app.locals.apiKey) {
		return res.status(401).json({ error: 'Unauthorized' });
	}
	return next();
}

export function sendUpdateManifest(_req, res) {
	try {
		const json = fs.readFileSync(paths.updateManifest, 'utf8');
		res.type('application/json').send(json);
	} catch {
		res.status(500).json({ error: 'Update manifest unavailable' });
	}
}
