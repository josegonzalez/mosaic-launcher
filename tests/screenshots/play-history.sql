-- The sample card's play history, in the Game Tracker's game_logs.sqlite
-- schema, with times counted back from when the shots are taken.
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tobu Tobu Girl', 'Game Boy (GB)/Tobu Tobu Girl.gb');
INSERT INTO rom(id, type, name, file_path) VALUES(2, '', 'µCity', 'Game Boy Color (GBC)/µCity.gbc');
INSERT INTO rom(id, type, name, file_path) VALUES(3, '', 'Thwaite', 'Nintendo Entertainment System (FC)/Thwaite.nes');
INSERT INTO rom(id, type, name, file_path) VALUES(4, '', 'Concentration Room', 'Nintendo Entertainment System (FC)/Concentration Room.nes');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 1500, strftime('%s', 'now', '-2 hours'));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 2400, strftime('%s', 'now', '-1 days'));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 1200, strftime('%s', 'now', '-3 days'));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 3600, strftime('%s', 'now', '-1 days', '-3 hours'));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 5400, strftime('%s', 'now', '-4 days'));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 1800, strftime('%s', 'now', '-6 days'));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(3, 900, strftime('%s', 'now', '-2 days'));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(3, 600, strftime('%s', 'now', '-5 days'));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(4, 1200, strftime('%s', 'now', '-20 days'));
