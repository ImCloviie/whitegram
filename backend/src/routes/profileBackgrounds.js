import fs from 'node:fs';
import path from 'node:path';
import express from 'express';
import multer from 'multer';
import {
	backgroundFilePath,
	readProfileIndex,
	writeProfileIndex,
	zoneKey,
} from '../config.js';
import { requireApiKey } from './updates.js';

const upload = multer({
	storage: multer.diskStorage({
		destination(req, _file, cb) {
			const dir = path.join(
				req.app.locals.profileBackgroundsDir,
				String(req.params.userId),
			);
			fs.mkdirSync(dir, { recursive: true });
			cb(null, dir);
		},
		filename(req, file, cb) {
			const zone = zoneKey(req.params.zone);
			const ext = path.extname(file.originalname).slice(1) || 'bin';
			cb(null, `${zone}.${ext}`);
		},
	}),
	limits: {
		fileSize: 32 * 1024 * 1024,
	},
});

export function createProfileBackgroundRouter() {
	const router = express.Router();

	router.get('/:userId', (req, res) => {
		const userId = String(req.params.userId);
		const index = readProfileIndex();
		const entry = index[userId];
		if (!entry) {
			return res.json({ userId, backgrounds: [] });
		}
		const backgrounds = Object.entries(entry.zones || {}).map(
			([zone, meta]) => ({
				zone,
				isVideo: Boolean(meta.isVideo),
				updatedAt: meta.updatedAt,
				url: `/api/v1/profile-backgrounds/${userId}/${zone}/file`,
			}),
		);
		return res.json({ userId, backgrounds });
	});

	router.get('/:userId/:zone/file', (req, res) => {
		const userId = String(req.params.userId);
		const zone = zoneKey(req.params.zone);
		if (!zone) {
			return res.status(400).json({ error: 'Invalid zone' });
		}
		const index = readProfileIndex();
		const meta = index[userId]?.zones?.[zone];
		if (!meta) {
			return res.status(404).json({ error: 'Background not found' });
		}
		const filePath = backgroundFilePath(userId, zone, meta.ext);
		if (!fs.existsSync(filePath)) {
			return res.status(404).json({ error: 'Background file missing' });
		}
		res.type(meta.mime || 'application/octet-stream');
		return res.sendFile(path.resolve(filePath));
	});

	router.put('/:userId/:zone', requireApiKey, upload.single('file'), (req, res) => {
		const userId = String(req.params.userId);
		const zone = zoneKey(req.params.zone);
		if (!zone) {
			return res.status(400).json({ error: 'Invalid zone' });
		}
		if (!req.file) {
			return res.status(400).json({ error: 'File is required' });
		}
		const ext = path.extname(req.file.filename).slice(1) || 'bin';
		const isVideo = String(req.body.isVideo || '').toLowerCase() === 'true'
			|| req.file.mimetype.startsWith('video/');
		const index = readProfileIndex();
		index[userId] ??= { zones: {} };
		index[userId].zones[zone] = {
			ext,
			isVideo,
			mime: req.file.mimetype,
			updatedAt: new Date().toISOJSON(),
		};
		writeProfileIndex(index);
		return res.json({
			userId,
			zone,
			isVideo,
			url: `/api/v1/profile-backgrounds/${userId}/${zone}/file`,
		});
	});

	router.delete('/:userId/:zone', requireApiKey, (req, res) => {
		const userId = String(req.params.userId);
		const zone = zoneKey(req.params.zone);
		if (!zone) {
			return res.status(400).json({ error: 'Invalid zone' });
		}
		const index = readProfileIndex();
		const meta = index[userId]?.zones?.[zone];
		if (meta) {
			const filePath = backgroundFilePath(userId, zone, meta.ext);
			if (fs.existsSync(filePath)) {
				fs.unlinkSync(filePath);
			}
			delete index[userId].zones[zone];
			if (!Object.keys(index[userId].zones).length) {
				delete index[userId];
			}
			writeProfileIndex(index);
		}
		return res.status(204).send();
	});

	return router;
}
