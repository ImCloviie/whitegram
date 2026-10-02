# Как работать с Whitegram (gitflow)

## Ветки

- `main` — стабильная ветка. Только релизы. Прямой пуш запрещен.
- `dev` — ветка разработки. Сюда сливаются готовые фичи через Pull Request.
- `feature/<название>` — ветка под каждую фичу. Создается от `dev`.

```
feature/login-e2ee ─┐
feature/music-fix ──┼──> dev ──(релиз)──> main
feature/ui-glass ───┘
```

## Обычный цикл фичи

```bash
# 1. Обнови dev
git checkout dev
git pull

# 2. Создай ветку фичи от dev
git checkout -b feature/название-фичи

# 3. Работаешь, каждая логическая часть — отдельный коммит
git add ...
git commit -m "scope: что сделано"

# 4. Пушишь ветку
git push -u origin feature/название-фичи

# 5. На GitHub открываешь Pull Request: feature/название-фичи -> dev
# 6. Второй человек смотрит, апрувит, жмет Merge (Squash или Merge — по ситуации)
# 7. Удаляешь ветку фичи после мержа
```

## Релиз

Когда `dev` в готовом состоянии:

1. Открываешь Pull Request `dev` -> `main`.
2. Второй человек проверяет, мержит.
3. Ставишь тег версии и собираешь Windows-билд в Releases.

## Правила

- Прямые пуши в `main` и `dev` запрещены (branch protection) — только через PR.
- PR в `main` требует апрув второго человека.
- Перед PR обнови ветку от `dev` (`git rebase dev` или `merge`), резолвь конфликты у себя.
- Клонировать репозиторий с сабмодулями:
  ```bash
  git clone --recursive https://github.com/ImCloviie/whitegram.git
  git submodule update --init --recursive
  ```

## Формат коммитов

`область: кратко что сделано`, например:

- `e2ee: one-time messages key rotation`
- `music: fix lyrics window crash`
- `docs: update README`
