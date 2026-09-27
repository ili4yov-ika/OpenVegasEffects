# Переводы интерфейса

Каталоги `openvegaseffects_ru.ts`, `openvegaseffects_ja.ts` и
`openvegaseffects_zh_CN.ts` содержат русский, японский и упрощённый китайский
переводы. `.qm` собираются и встраиваются в приложение через CMake или qmake.

После добавления переводимых строк выполните в окружении Qt и компилятора:

```powershell
lupdate OpenVegasEffects.pro -no-obsolete
python tools/validate_translations.py
```

Заполните новые строки перед сборкой. Сохраняйте подстановки `%1`, `%n`,
переносы строк, HTML-ссылки и шаблоны файлов `(*.ext)`. Русские сообщения с `%n`
требуют трёх форм, японские и китайские — одной. Имена ресурсов и служебные
ключи стилей в `.ui` помечайте `notr="true"`.

Проверка скомпилированных каталогов в настроенной сборке тестов:

```powershell
cmake --build build/timeline-tests --target translation_regression
ctest --test-dir build/timeline-tests -R translation_regression --output-on-failure
```

Тест проверяет загрузку всех трёх языков, возврат к английскому, контексты
диалога выбора цвета и статусов сплеша, а также формы множественного числа.
