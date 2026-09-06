import express from 'express';
import { readE2eeKeys, writeE2eeKeys } from '../config.js';

export function createE2eeKeysRouter() {
	const router = express.Router();

	const checkAuth = (req, res, next) => {
		const key = req.get('X-API-Key') || req.get('X-Whitegram-Key');
		if (req.app.locals.apiKey && key !== req.app.locals.apiKey) {
			return res.status(401).json({ error: 'Unauthorized' });
		}
		return next();
	};

	router.use(checkAuth);

	// POST /api/v1/e2ee/keys/register
	router.post('/register', (req, res) => {
		const { userId, publicKey } = req.body || {};
		if (!userId || !publicKey || typeof publicKey !== 'string') {
			return res.status(400).json({ error: 'userId and publicKey required' });
		}
		const keyStr = String(publicKey).trim();
		if (keyStr.length < 32 || keyStr.length > 512) {
			return res.status(400).json({ error: 'Invalid publicKey format' });
		}
		const uid = String(userId).trim();
		const keys = readE2eeKeys();
		keys[uid] = {
			publicKey: keyStr,
			updatedAt: Date.now(),
		};
		writeE2eeKeys(keys);
		return res.json({ ok: true, userId: uid, updatedAt: keys[uid].updatedAt });
	});

	// GET /api/v1/e2ee/keys/:userId
	router.get('/:userId', (req, res) => {
		const uid = String(req.params.userId).trim();
		const keys = readE2eeKeys();
		const entry = keys[uid];
		if (!entry || !entry.publicKey) {
			return res.status(404).json({ error: 'Key not found', userId: uid });
		}
		return res.json({
			ok: true,
			userId: uid,
			publicKey: entry.publicKey,
			updatedAt: entry.updatedAt,
		});
	});

	return router;
}
