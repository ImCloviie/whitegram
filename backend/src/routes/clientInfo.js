import fs from 'node:fs';
import { paths } from '../config.js';

export function sendClientInfo(_req, res) {
	try {
		const json = fs.readFileSync(paths.clientInfo, 'utf8');
		res.type('application/json').send(json);
	} catch {
		res.status(500).json({ error: 'Client info unavailable' });
	}
}
