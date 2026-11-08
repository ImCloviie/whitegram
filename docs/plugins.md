# Плагины Whitegram

Система Python-плагинов Whitegram сделана по мотивам идеи
[exDesktop](https://github.com/metawka/exDesktop) (интерпретатор CPython
встроен прямо в клиент) и адаптирована под Whitegram.

## Как это работает

- В `Whitegram.exe` вкомпилирован движок `Whitegram::Plugins`
  (`Telegram/SourceFiles/ayu/features/whitegram_plugins/`).
- При запуске клиент поднимает встроенный интерпретатор Python из папки
  `python/` рядом с `Whitegram.exe` и загружает хост `whitegram_host.py`.
- Каждый плагин — это один файл `*.plugin`: обычный Python-скрипт, в котором
  C++-сторона статически (через AST, не выполняя код) читает метаданные из
  констант `__id__`, `__name__`, `__version__`, `__author__`,
  `__description__`, `__permissions__`.
- Плагины хранятся в `tdata/whitegram/plugins/plugins/`, их настройки —
  в `tdata/whitegram/plugins/settings/`.

## Возможности v1

| Механизм | Описание |
|---|---|
| События сообщений | `on_update_hook(name, update)` — новые входящие/исходящие сообщения |
| Перехват отправки | `on_send_message_hook(params)` — отмена или модификация текста до отправки |
| Слэш-команды | `self.register_command("ping", handler)` → `/ping args` в любом чате (сообщение не отправляется) |
| Меню сообщений | `add_menu_item(MenuItemType.MESSAGE_CONTEXT_MENU, ...)` — пункт в правом меню сообщения |
| Черновики | `on_draft_transform(text)` + `Ctrl+Alt+P` в поле ввода |
| Настройки плагина | `create_settings()` → переключатели/кнопки в боксе настроек плагина |
| API клиента | `toast`, `send_message`, `edit_message`, `peer_info`, `history`, `show_alert`, `open_url`, `copy`, `run_on_main` |

## Безопасность

- **Trust**: у каждого плагина считается SHA-256. Плагины, установленные из
  настроек, доверяются автоматически; файлы, скопированные вручную, показываются
  как «непроверенные» и не грузятся, пока не включите тумблер
  «Разрешите плагины из неизвестных источников» или не нажмёте «Доверять».
- **Безопасный режим** выключает все плагины (тумблер в настройках).
- **Watchdog**: если плагин заблокировал интерфейс больше чем на 10 секунд,
  при следующем зависании включается автоматический безопасный режим.
- После некорректного завершения (краш) следующий запуск тоже уходит
  в безопасный режим.

Плагины исполняются с правами вашего пользователя и не изолированы
песочницей — ставьте их только из источников, которым доверяете.

## Пример плагина

```python
__id__ = "hello"
__name__ = "Hello, Whitegram!"
__version__ = "1.0"
__author__ = "Вы"
__description__ = "Первый плагин"

from whitegram import BasePlugin, MenuItemType

class HelloPlugin(BasePlugin):
    def on_plugin_load(self):
        self.register_command("ping", lambda ctx: f"Pong! {ctx.args}")
        self.add_menu_item(
            MenuItemType.MESSAGE_CONTEXT_MENU,
            "👍 Реакция",
            on_click=self.reply_like,
        )

    def reply_like(self, context):
        self.send_message("👍", context["dialog_id"],
                          reply_to=context.get("message_id", 0))

    def on_update_hook(self, name, update):
        if name == "updateNewMessage":
            self.logger.debug(update["text"])
```

Готовые примеры лежат в `plugins/examples/` (устанавливаются кнопкой
«Установить плагин из файла…» в Настройках → Whitegram → Плагины).

## Сборка

Движок собирается автоматически, если CMake находит Python с заголовками и
импорт-библиотекой (`find_package(Python3 COMPONENTS Interpreter Development)`).

- Windows: в `out/<config>/python/` копируется рантайм (`python3xx.dll`,
  `DLLs/`, `Lib/`) и SDK-скрипты в `python/whitegram/`. Используется
  `/DELAYLOAD`, поэтому без папки `python/` клиент запускается как обычно.
- Если Python Development не найден, движок выключается (в настройках
  отображается «Эта сборка собрана без Python-движка»).
- Отключить целиком: `-D WG_PYTHON_PLUGINS=OFF`.

## Ограничения v1 (план на v2)

- События: только новые сообщения и lifecycle-события приложения.
- Пункты меню: только контекстное меню сообщений.
- Нет реестра плагинов на бэкенде (каталог/автообновление) и подписок
  из вашей backend-системы.
- Нет raw MTProto-запросов из Python и media-send.
- Права (`__permissions__`) декларируются, но не ограничиваются принудительно.
