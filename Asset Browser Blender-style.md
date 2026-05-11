Asset Browser Blender-style для Unreal Engine — что именно люди хотят
Что говорят в чатах
Ключевой пост, с которого всё началось — «Does Unreal have anything like Blender's Asset Browser for centralized reusable assets?» (r/unrealengine, ~10 мес. назад). Автор прямо описывает, чего не хватает: «You can mark models, materials, HDRIs, collections, etc., as assets». В обсуждении пользователи называют ближайший аналог — Polygonflow Dash, но указывают на ограничения: тяжёлый, требует своих preview-файлов, AI-тегирование плохо работает на больших библиотеках (>4000 ассетов), периодически «теряет» контент при обновлениях, тесно завязан на Megascans/Dekogon.
Параллельно на форумах Epic и в r/unrealengine собираются однотипные жалобы и пожелания, складывающиеся в общую картину запроса:

Vault Favorites / тэги в библиотеке («Unreal Engine > Library — It needs some love and attention» на forums.unrealengine.com): пользователи просят отмечать купленные на Fab ассеты как избранные, ставить кастомные теги, сортировать по проектам.
Кастомные теги и метаданные на ассетах («Ability to put tags on the assets in our editor», forums.unrealengine.com 2017 — до сих пор открыт). Asset Registry tags существуют, но «underrated and hard to use» (свежий пост на r/unrealengine), нужен GUI. Появился сторонний Tech Art Corner Metadata Tags Editor — это подтверждение спроса.
Цвет папок, коллекции, избранное — есть, но фрагментарно. В тредах «Set a custom color for ANY folder», «How do you add a material to the Favorites section», «Stop Wasting Time in UE5 (Use Favorite Folders!)» жалуются на отсутствие иерархии каталогов, невозможность сделать Favorite на отдельный материал/меш (только папку).
Кросс-проектная библиотека («Sharing materials between projects», старый тред с 4.14 + Facebook-обсуждение Archviz «Handling large Unreal asset libraries efficiently»): люди хотят держать одну центральную папку с материалами/моделями/HDRI и подключать её ко всем проектам без Migrate.
Поиск по виду, а не по имени файла: миниатюры ассетов в UE существуют, но люди хотят полноценный grid с превью и фильтрами как у Blender — по типу, тегу, цвету, полигонажу, размеру текстур.
Drag & drop коллекций целиком (props-набор, набор материалов с текстурами и матинстансами, набор Niagara-эффектов) — сейчас перетаскивается только один ассет.

Что именно подразумевается под «Blender-style» — короткая референсная карта
В Blender Asset Browser даёт:

«Mark as Asset» правой кнопкой по любому датаблоку (mesh, material, world/HDRI, node group, pose, geometry nodes setup, collection).
Каталоги (Catalogs) — иерархия, не привязанная к файловой системе, плюс теги.
Превью с кастомным рендером миниатюры.
Подключение нескольких внешних библиотек (папок) — глобальные, видны во всех проектах.
Drag & drop в 3D-вьюпорт — материал на объект, mesh в сцену, поза на ригу, нод-группа в Geometry/Shader Editor.
Pose Library и Geometry Nodes как полноценные «асет-классы».

Что в этом плагине должно быть для Unreal
1. Универсальная отметка «Mark as Asset»
Правый клик по любому ассету в Content Browser добавляет его в библиотеку плагина: Static Mesh, Skeletal Mesh, Material/Material Instance, Texture/HDRI, Niagara System, Sound, Animation, Blueprint Class, MetaSound, PCG Graph, Niagara Module. Должна работать и на наборах (выделил 30 материалов — добавил все одной командой).
2. Каталоги, не привязанные к файловой системе
Иерархическое дерево каталогов с drag-and-drop переносом ассетов между ними. Каталог хранится отдельно от структуры папок Content/, чтобы не ломать пути и редиректоры. Под капотом — отдельная база (SQLite или JSON в Saved/AssetLibrary), плюс Asset Registry tags для поиска.
3. Теги, цвета, рейтинги, заметки
GUI-редактор тегов (как у Tech Art Corner, но интегрированный): мульти-выделение, batch-add/remove, цветовые метки, 5-звёздный рейтинг, текстовые заметки. Поиск с булевой логикой (material AND wood NOT wet).
4. Глобальные/общие библиотеки между проектами
Главный запрос из треда «centralized reusable assets». Подключение внешней папки (локальной или сетевой/Dropbox/SMB) как «библиотеки». Ассеты оттуда показываются в браузере, а при перетаскивании в проект автоматически копируются (или линкуются через Plugin Content / Shared Content Plugin), фиксы редиректов, перенос зависимостей.
5. Превью-сетка с настоящими 3D-миниатюрами
Большая сетка thumbnails (256–512px), кастомный рендер превью с собственного освещения и поворота камеры, для материалов — превью на сфере/кубе/плоскости на выбор, для HDRI — sphere preview. Возможность задать собственный preview image (как в Blender — «Generate Preview / Custom Preview»).
6. Drag & drop в любой контекст
Перетаскивание материала на меш в Viewport, текстуры на параметр Material Instance, Niagara-эффекта в уровень, позы на Skeletal Mesh, PCG-графа в World Partition тайл, целой «коллекции» (например, Forest Pack — 40 деревьев + материалы + decals) одним жестом.
7. Asset Sets / Collections как самостоятельная сущность
Группа ассетов, которая дропается одной операцией (lighting kit = HDRI + 3 directional lights + post-process volume + материалы пресетов). Под капотом — Blueprint-spawner или Level Snapshot.
8. Смарт-фильтры и сохранённые поиски
Аналог blender'овских filter presets: «все материалы с Tessellation», «все меши >50k треугольников», «все текстуры >4K», «все ассеты, изменённые за неделю». Фильтр по техническим свойствам ассета (verts, tris, LOD count, virtual texture, lightmap UV present, collisions present), не только по имени.
9. Pose Library и Animation Library
Аналог Blender Pose Library, но для UE: запись позы скелета прямо из Persona/Sequencer, drag-and-drop позы на Skeletal Mesh, blending. Для аниматоров это закрывает огромный пробел (Mixamo + плагин = быстрая раскладка).
10. AI-теги, но локальные и быстрые
Жалоба на Dash в Discord — AI-tagging «не справляется с >4000 ассетов» и требует онлайна. Решение: локальный CLIP/SigLIP для visual-tagging миниатюр без отправки в облако, batch-режим в фоне, ручная коррекция.
11. Версии и история ассета
Маленькая боковая панель: когда импортирован, кто менял (если есть VCS), список зависимостей, обратные ссылки (что использует этот материал). Сейчас Reference Viewer есть, но он отдельный — должен быть встроен в карточку ассета.
12. Marketplace/Fab-aware режим
Видеть, какие ассеты библиотеки куплены на Fab, есть ли обновление, changelog, кнопка «Re-import latest version». Это закрывает второй большой запрос форума («never knowing when assets were updated»).
13. Multi-project / team mode
Опционально — синхронизация библиотеки тегов и каталогов между членами команды через Git/Perforce/Diversion (хранение в текстовом JSON, чтобы мерджилось).
14. Hotkeys и Quick Picker
Spotlight-style окно по Ctrl+Space: начинаешь печатать «brick», получаешь grid с превью совпадений, Enter — drag в текущий уровень/материал. Это тот workflow, который у Blender-юзеров вызывает рефлекс.

Как это должно выглядеть (UI-набросок)
Думайте о docked-панели рядом с Content Browser, но другой по концепции. Сверху — узкая полоса инструментов: переключатель Library Source (Project / Shared / Fab Vault / Custom Folders), кнопка Mark as Asset, поиск с булевыми операторами, фильтр-чипы (Type, Tag, Color, Rating, Recent).
Слева — тонкая колонка с деревом каталогов (Catalogs), отдельно от Content/; каждый каталог имеет иконку и цвет, на ней счётчик ассетов. Под деревом — список Smart Filters / Saved Searches и блок Tags (облако тегов с кликабельными чипами — клик включает фильтр).
Центр — сетка крупных thumbnails (slider масштаба, как у Blender — 32→512 px). Каждая карточка: большая превьюшка, в углу иконка типа (mesh/material/HDRI/Niagara/Anim), цветная полоска каталога, маленькие звёздочки рейтинга, бейдж «Fab Update Available», бейдж «Used in N levels». Контекстное меню: Edit Tags, Set Custom Preview, Add to Asset Set, Show Dependencies, Migrate to Project.
Справа — inspector-панель карточки: крупное превью с возможностью покрутить камеру (для меша), технические свойства (tris, LOD, материалы, текстуры, размер на диске), теги (редактируемые чипы), заметки, секция Dependencies (referenced by / references), история (imported / modified / source file path), кнопки Drag-to-Level / Apply-to-Selection.
Снизу опционально — Drop Zone: горизонтальная полоска с последними использованными и Pinned-ассетами для постоянного быстрого доступа.
Глобальный шорткат Ctrl+Space открывает Quick Picker — overlay-окно по центру экрана с поиском и сеткой превью, drag прямо во вьюпорт, Esc закрывает.

Итог по позиционированию
Plugin должен закрывать четыре боли одновременно: (1) тяжёлый Content Browser, привязанный к структуре папок; (2) отсутствие нормальных тегов и кросс-проектной библиотеки; (3) перетаскивание только одиночных ассетов вместо коллекций; (4) дороговизна и тяжеловесность Polygonflow Dash для тех, кому не нужна вся его экосистема Megascans-сцен. Если хочешь, могу набросать MVP-фичсет на 1.0 с приоритетами и оценкой, что реалистично сделать через Editor Subsystem + Slate + Asset Registry на UE 5.5/5.6.








Прямые цитаты по Asset Browser-теме (тред r/unrealengine, 1lsh8dr, июль 2025)
Автор поста u/Candid-Pause-1755: «In Blender you can mark models, materials, HDRIs, collections, etc. as assets. Then drag and drop them into any project. You can also organize them with tags and categories. It just works. But in Unreal, every time I want to reuse assets from another project, I have to either re-import them, migrate them manually, or set everything up again from scratch. It's slow and messy. Is there a way to set up a central asset library I can reuse across projects without having to re-import or rebuild everything each time?»
Ключевые ответы:

u/FayHallow — «Not really. The way to go is creating your own Plugin with the content you want to reuse» (ссылается на туториал Epic «creating-a-content-only-plugin»).
u/remarkable501 — «There isn't a shared asset container in Unreal. You can make a base project template, you can have one giant project, you can pay for git hub to save all your project, or make yourself a plugin that contains your assets. It boils down to how the engine was made and coded.»
u/wowqwop — «The Dash plugin has something like this its free aswell. We have been using it in my teams project and it works pretty well» (ссылка на polygonflow.io/content-browser).
u/bradleychristopher — «Create a Content Only plugin and add it to the Engine folder so you would just need to enable the plugin for each project. This obviously means you run the risk of modifying assets you are using in other projects.» Автор соглашается: «From all the comments, I feel like the Content Only Plugin is the best option I got for now».
u/DisplacerBeastMode — «I've been wondering the same. I would love to have some centralized asset management program with a fast search and filtering options.»
u/The_Earls_Renegade — про symbolic links к одной папке между проектами (хак, не решение).
u/Candid-Pause-1755 (ОП) — «Made a little tool to create symlinks from my asset folder for each new project. Works okay, but linking each folder manually is not ideal. Really wish Unreal had something like Blender's Asset Browser.»
u/extrapower99 — самый развёрнутый ответ, важная деталь: «If u just want to choose assets u have a preview in unreal u dont and there is no nice labelling etc. like in blender, u need good file names». То есть отсутствие thumbnail-превью при выборе из внешнего источника — конкретная боль.
u/lvnariss — упоминает: «You can create a separate project and migrate assets — but symlinks делают это ненужным».
u/TomeLed — «I created an engine plugin. It works pretty well but it's annoyingly always at the bottom of the list, stuffed with all the other engine junk and I couldn't figure out how to favourite or anything for quick access. It's just horrible, seems so basic.»

Что подтвердилось из других тредов
Тред «What plugins do you wish existed» (r/unrealengine, 1evus9b):

u/peterfrance (повторно): «Keybind-based moving/rotating/scaling actors instead of using the Gizmo. Blender does it perfectly: press G to start moving relative to camera, optionally press X/Y/Z to lock to one axis, then move your mouse and click. So much more efficient than that stupid gizmo!» (Blender-style контролы — отдельный пункт, но это уже другая идея.)
u/mours_lours — «A good plugin for level designing. like if I press shift + ? it Changes from grid mode to free. You could set hotkeys for different mesh reference So I can change between them with the press of a button. Foliage mode kind of solves this but it's not grid based.»
u/Sweetozmanthus — «Movable landscape layers».

Тред «What plugin are missing on the marketplace» (r/unrealengine, 1kl1l1v):

u/El_HermanoPC — «A plugin that lets you save and load text files in editor. Notes regarding all the blueprints in a particular folder, save as readme.txt, show up in the editor like any other asset. Alternatively task tracking in editor — Kanban board, in-editor Trello support. Mark an asset as in-progress, completed, awaiting review.»
u/DudeBroJustin — «Data table management! The current datatable is a mess. You can't drag an item up a page, it just glitches and doesn't move. Colored rows, folders/collapsing, multi edit.»
u/davis3d — «A plugin that lets you copy actors from the blueprint hierarchy to the level. For example, a bunch of rock meshes in a blueprint that you need in the level» — это смежно: запрос на «коллекции» ассетов одной кнопкой.

Тред «What code plugin would you love to use that doesn't exist» (16ylk46):

u/Cold_Meson_06 — «One that fixes the content browser, too much pain and suffering to use it. Why I always need to click "all" to have a useful search? Why my ctrl+space browser is not used when I click "browse to asset"? Why does it randomly opens in a new window? Why sometimes go to reference makes me swap editor windows?»
u/BULLSEYElITe — «To make custom folder hierarchies without actually moving stuff» — буквально blender-каталоги поверх Content/.
u/Squirrel__Knight — «A plugin which saves opened tabs order in editor and restores them on start would be a godsend.»

Тред «Most pressing issue» (1hwvqzn) — не про Asset Browser напрямую, но добавляет смежный контекст: жалобы на стабильность, build times, документацию, batch actions для материалов.
Уточнённая картина Asset Browser-плагина после чтения комментариев
Запросы людей сводятся к семи конкретным фичам, которые нужно реализовать одной коробкой:
1. Centralized asset library across projects. Главный, повторяющийся в каждом треде запрос. Решения, которые люди уже сами пробовали: (а) Content-Only Plugin, (б) symlinks/junctions на Windows, (в) base project template, (г) один гигантский проект, (д) Dash. Все они компромиссы. Плагин должен дать готовое решение «настроил папку → она доступна во всех проектах».
2. Mark as Asset для любого типа. В точности по аналогии с Blender: правый клик по mesh/material/HDRI/Niagara/Blueprint → «Add to Library». Без переименования, перемещения, без копирования файлов.
3. Catalogs / virtual folders без перемещения файлов. Фраза «custom folder hierarchies without actually moving stuff» прямо подтверждает запрос. Это слой поверх физической Content/, не меняющий пути и не ломающий редиректоры.
4. Tags + fast filtered search. «Fast search and filtering options». Жалоба «I always need to click All to have a useful search» подтверждает, что встроенный поиск считают медленным/неинтуитивным.
5. Previews/thumbnails для внешних источников. «In Unreal you don't have a preview, you need good file names» — конкретный пейн, прямой аргумент к фиче 3D-thumbnails grid с кастомным освещением.
6. Favorites / quick access / pinning. Цитата TomeLed «engine plugin always at the bottom of the list, couldn't figure out how to favourite or anything for quick access» — буквальная жалоба на отсутствие пиннинга. Нужна область quick access, переподключение «по щелчку».
7. Asset Sets / Collections drag-as-group. Запрос u/davis3d — «копировать пачку рок-мешей из BP сразу на уровень» — близкий по смыслу к Blender-collections, перетаскиваемых как один объект. Кейс «forest pack = 40 мешей + материалы + decals + lighting preset одной операцией».
Дополнительно из побочных комментариев — «note files / readme прямо как ассет», «in-editor Kanban с пометкой WIP / Done на ассете», «colored rows / folders в data tables» — это смежные QoL-функции, которые могут стать частью того же плагина (rating + status + notes на карточке).
Что меняется в дизайне после прочтения первоисточников
По сравнению с моим прошлым описанием UI стоит усилить три вещи, которые в комментариях звучат сильнее всего:

Кросс-проектность не как «опция», а как корневая концепция плагина. Левая панель должна по умолчанию показывать глобальную библиотеку, а не «текущий проект». Источник — общая папка (локальная или сетевая), под капотом плагин делает прозрачный Content-Only-mount или symlink, в карточке ассета указано «Source: D:\SharedLib\Materials\Brick». Перетаскивание во вьюпорт автоматически копирует/линкует ассет в проект.
Обязательный 3D-preview даже до импорта. Жалоба extrapower99 на «нет preview, только filename» означает, что плагин должен уметь рендерить превью fbx/uasset файлов из внешней папки без их импорта в проект — отдельный thumbnail cache в библиотеке.
Pinning / Favorites первого класса. TomeLed формулирует прямо: основная боль — «engine plugins всегда внизу списка, нет фавориток». Значит, на верхнем уровне плагина — секция «Pinned» (как Spotify-bottom-bar) с любым ассетом, материалом, набором или каталогом, который пинуется одним кликом.

И отдельно — понятная дифференциация против Polygonflow Dash, который в треде упоминают как ближайший аналог. Dash хорош как world-building suite (scatter, blend materials, vines, megascans-интеграция), но не закрывает базовую боль кросс-проектной библиотеки в чистом виде, требует своих превью, сильно весит и заточен под среды. Новый плагин должен быть минималистичным, узким и быстрым: только asset management, как Blender Asset Browser, без world-building.