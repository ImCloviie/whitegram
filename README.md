# Whitegram

<img src="branding/icon/png/icon_640.png" width="128" alt="Whitegram">

Неофициальный десктопный клиент Telegram для Windows на базе [AyuGram](https://github.com/AyuGram/AyuGramDesktop).

## Фишки

**Приватность**
- Гибкий ghost-режим
- История сообщений и анти-отзыв (удаленные и измененные сообщения никуда не пропадают)
- Сквозное шифрование Whitegram (E2EE) и одноразовые фото/видео
- Режим стримера

**Внешний вид**
- Стеклянный эффект и дополнительные настройки оформления
- Кастомный фон профиля
- Кастомизация шрифта
- Музыкальный остров, сохраненная музыка и тексты песен

**Удобство**
- Локальный Telegram Premium
- Встроенный переводчик
- Кастомные архивы и фильтры чатов
- Обход блокировок соединения

## Скачивание

Пока что готовые сборки есть **только для Windows**:

- Вкладка [Releases](https://github.com/ImCloviie/whitegram/releases) — скачай последний `Whitegram.exe` (или архив с ним)

Сборок для macOS и Linux пока нет.

## Сборка из исходников

### Windows

Следуй [руководству по сборке](docs/building-win.md).

### Linux / macOS

Готовых сборок нет, но можно собрать вручную:

- Linux: [docs/building-linux.md](docs/building-linux.md)
- macOS: [docs/building-mac.md](docs/building-mac.md)

Для сборки нужен API ID и API hash — как их получить, описано в [docs/api_credentials.md](docs/api_credentials.md).

## Основано на

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)
- [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop)

## Лицензия

См. файл [LICENSE](LICENSE).
