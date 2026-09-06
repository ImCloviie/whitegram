# Whitegram Backend

HTTP-сервис для Whitegram Desktop:

- **Обновления клиента** — формат совместим с Telegram Desktop (`GET /current`, `GET /current4`)
- **Фоны профиля** — загрузка и раздача фонов другим пользователям Whitegram

## Быстрый старт

```powershell
cd E:\WhitegramProject\backend
copy .env.example .env
npm install
npm run dev
```

Сервер по умолчанию: `http://127.0.0.1:8787`

## Обновления

Манифест: `data/updates/current.json`

Формат (как у официального tdesktop):

```json
{
  "win64": {
    "stable": {
      "released": 7000010,
      "link": "/updates/Whitegram-7000010.exe"
    }
  }
}
```

Положите установщик в `data/updates/` и укажите версию выше текущей в клиенте (`Telegram/build/version`).

Клиент Whitegram читает префикс `http://127.0.0.1:8787` и запрашивает `/current4` на Windows.

## Beta-версия в клиенте

Файл: `data/client-info.json`

```json
{
  "betaVersion": "2",
  "betaLabel": "beta"
}
```

Клиент показывает `beta версия 2` (RU) или `beta 2` (EN) справа от **Whitegram** в боковом меню.
Можно задать полный текст через `"displayVersion": "beta 7.0.9-b2"`.

## Фоны профиля

| Метод | Путь | Описание |
|-------|------|----------|
| GET | `/api/v1/client-info` | Beta-версия для отображения в клиенте |
| GET | `/api/v1/profile-backgrounds/:userId` | Список фонов пользователя |
| GET | `/api/v1/profile-backgrounds/:userId/:zone/file` | Файл фона (`top`, `bottom`, `full`) |
| PUT | `/api/v1/profile-backgrounds/:userId/:zone` | Загрузка (multipart, поле `file`) |
| DELETE | `/api/v1/profile-backgrounds/:userId/:zone` | Удаление |

Загрузка и удаление требуют заголовок:

```
X-Whitegram-Key: <значение из .env WHITGRAM_API_KEY>
```

Чтение публичное — любой клиент Whitegram может показать фон другого пользователя.

## Переменные окружения

| Переменная | По умолчанию | Описание |
|------------|--------------|----------|
| `PORT` | `8787` | Порт |
| `HOST` | `127.0.0.1` | Хост |
| `WHITGRAM_API_KEY` | `change-me-in-production` | Ключ для upload/delete |
| `DATA_DIR` | `./data` | Хранилище файлов |

## Health check

```
GET /health
```
