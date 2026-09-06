# Whitegram Release

Скрипт для версии, упаковки и выкладки на VDS.  
**Сборку делаешь сам в Visual Studio** — скрипт не компилирует по умолчанию.

## Рекомендуемый процесс

### Шаг 1 — выставить версию

```bat
cd /d E:\WhitegramProject
release.bat version 1.0.11
```

Обновит:
- `Telegram/build/version`
- `Telegram/SourceFiles/core/version.h`
- `backend/data/client-info.json`

> Команды ниже — для **cmd** (Командная строка). Сначала `cd /d E:\WhitegramProject`.

### Шаг 2 — собрать в Visual Studio

1. Открой **x64 Native Tools Command Prompt for VS 2022** (или обычный VS)
2. Открой проект:
   - `E:\WhitegramProject\out\Telegram\Telegram.vcxproj`
   - или solution из `E:\WhitegramProject\out\`
3. **Configuration:** `Release`
4. **Platform:** `x64`
5. **Build → Build Telegram**
6. **Build → Build Packer** (один раз или после смены ключей подписи)

Должны появиться файлы:

```
E:\WhitegramProject\out\Release\Whitegram.exe
E:\WhitegramProject\out\Release\Updater.exe
E:\WhitegramProject\out\Release\Packer.exe
```

> **Важно:** сначала `version`, потом сборка. Иначе в exe будет старый номер версии.

### Шаг 3 — упаковать и залить на VDS

```bat
release.bat ship
```

Скрипт:
1. Создаст `tx64upd*` (подписанный pack)
2. Обновит `backend/data/updates/current.json`
3. Загрузит pack + `current.json` + `client-info.json` на VDS

Без upload:

```bat
release.bat ship --no-push
```

---

## Команды

| Команда | Когда использовать |
|---------|-------------------|
| `release.bat status` | Посмотреть версию и наличие exe/pack |
| `release.bat version 1.0.11` | Выставить pre-beta **перед** сборкой |
| `release.bat bump patch` | +1 к patch в файлах версии |
| `release.bat ship` | **После VS build** — pack + VDS |
| `release.bat ship --no-push` | Только pack локально |
| `release.bat pack` | Только pack |
| `release.bat push` | Только upload (pack уже есть) |
| `release.bat build` | Опционально: cmake/msbuild вместо VS |

Алиасы: `set` = `version`, `publish` / `deploy` = `ship`

---

## Пример полного релиза 1.0.11

```bat
cd /d E:\WhitegramProject
release.bat version 1.0.11
```

→ собери **Release | x64** в Visual Studio

```bat
release.bat ship
```

---

## Версии

| pre-beta | AppVersion |
|----------|------------|
| 1.0.11 | 7000019 |
| 1.0.x | 7000008 + x |
| 1.1.0 | 7010100 |

---

## Первый раз

```bat
pip install paramiko cryptography
python backend\deploy\setup_update_signing.py
```

Ключи подписи: `backend/deploy/keys/` — не удалять.

Библиотеки сборки (Qt и др.) должны лежать в `E:\Libraries\win64\` (рядом с репозиторием, не внутри него).

---

## Перегенерация CMake (`configure.py force`)

`force` удаляет solution из `out\`, но **сохраняет** `out\Debug` и `out\Release`.

Запускай из **x64 Native Tools Command Prompt for VS 2022**:

```bat
cd /d E:\WhitegramProject
python Telegram\configure.py force
```

Если ошибка **Neither Qt6 nor Qt5 is found** — проверь, что есть папка вроде `E:\Libraries\win64\Qt-5.15.18`.

---

## Опционально: сборка через скрипт

Если не хочешь VS:

```bat
release.bat build
release.bat ship
```

Или всё одной командой (устаревший режим):

```bat
release.bat release 1.0.11 --build
```
