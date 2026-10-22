# Лабораторная работа №2. Камера и освещение

Вариант 10 — правильный додекаэдр. Реализация на C++20, Vulkan, GLFW, GLM,
ImGui и Vulkan Memory Allocator. Выполнена базовая часть и четыре дополнительных
задания. Текст ниже объясняет реализацию и порядок демонстрации на защите.

## Исходный проект и ветки

Работа продолжает `mariakotikw/vulkan-lab1-v10`, ветку `lab1-v10`, коммит
`7f24674`. В рабочей папке `lab2` создан отдельный локальный репозиторий и ветка
`lab2-v10`. Соседняя папка `lab1`, её история и незакоммиченное изменение
`Animation::offset()` сохранены. Вторая работа основана на закоммиченной версии
первой, включая исходную пространственную анимацию. Публикация в GitHub не
выполнялась. Исторические `README.md`, `LAB1.md` и материалы `docs/screenshots`
сохранены; инструкции для второй работы находятся в этом документе.

## Соответствие заданию

| Требование | Реализация и место в коде |
|---|---|
| Камера: XYZ, yaw, pitch, FOV, near/far, сброс | `ProjectionState` в `source/application_state.hpp/.cpp`, `viewMatrix()`, `projectionMatrix()`, `reset()`; раздел Camera в `application_ui.cpp` |
| Перспективная и ортографическая проекции, Vulkan depth [0,1] | `perspectiveRH_ZO`, `orthoRH_ZO`, единственный переворот Y в `projectionMatrix()` |
| Правильный додекаэдр, 12 пятиугольников, индексированная геометрия | `makeDodecahedron()` и `makeFlatDodecahedron()` в `source/dodecahedron.cpp` |
| Плоские наружные нормали | `makeFlatDodecahedron()`: 60 вершин с отдельной нормалью каждой грани |
| Неравномерный масштаб и normal matrix | `SceneState::objectUniforms()`, inverse transpose Model; `shaders/dodecahedron.vert` |
| Две независимые модели и материалы | `ObjectState`, `Material`, `SceneState::objects`; два Object UBO и два Object descriptor set |
| Ambient, Lambert, Blinn–Phong | `shaders/dodecahedron.frag`, `blinnPhong()` |
| Directional Light | `DirectionalLight`, `SceneUniform`, раздел Directional Light |
| Доп. №1: Scene и Object Descriptor Sets | `createDescriptors()`, `updateSceneDescriptors()`, `createPipeline()`, `render()` в `source/application.cpp` |
| Доп. №2: Point Lights в SSBO | `PointLight`, `GpuPointLight`, `SceneState::addPointLight/removePointLight`, `ensureCapacity()`, GLSL `Points.lights[]` |
| Доп. №3: Spot Lights в SSBO | `SpotLight`, `GpuSpotLight`, `sanitize()`, `spotConeFactor()`, GLSL `Spots.lights[]`, `coneFactor()` |
| Доп. №4: 3D-маркеры и конусы, unlit | `makeLightGizmos()` в `source/light_gizmos.cpp`, `shaders/gizmo.vert/.frag`, два gizmo pipeline |
| Управление добавлением, удалением и параметрами света | `drawControls()` в `source/application_ui.cpp`, разделы Point Lights / Spot Lights |
| Безопасное обновление буферов | fence в `graphics::internal::prepare()` до `application::update()`, VMA flush, действительные буферы при нуле источников |
| Depth test, culling и resize | `createPipeline()`, `rebuildSwapchain()`, `main.cpp`; отдельные семафоры для изображений swapchain |
| Тесты математики, геометрии и ABI | `tests/dodecahedron_tests.cpp`, `application_state_tests.cpp`, `lighting_tests.cpp`, `verify_shader_layout.py` |
| Графические проверки | `scripts/verify-vulkan.ps1`, `tests/verify_controls.ps1`, `tests/verify_lighting_controls.ps1`, `tests/verify_images.py` |

## Архитектура

Сохранено разделение исходного Starter App:

- `main.cpp` создаёт GLFW/ImGui, обрабатывает события и вызывает `prepare → update → render → submitAndPresent`.
- `graphics_internal.cpp` владеет instance/device, swapchain, depth image,
  framebuffer, command buffer, синхронизацией и Vulkan backend ImGui.
- `graphics.cpp` создаёт и освобождает mapped buffers через VMA и выполняет flush.
- `application.cpp` владеет геометрией, UBO/SSBO, дескрипторами и тремя pipeline;
  формирует и записывает данные сцены и команды рисования.
- `application_state.cpp` содержит преобразования, камеру, независимую анимацию,
  CPU-состояние сцены и подготовку uniform-данных.
- `lighting.hpp/.cpp` определяет материалы, источники, GPU-структуры и ограничения параметров.
- `light_gizmos.cpp` строит геометрию маркеров и линий конусов в мировом пространстве.
- `application_ui.cpp` содержит элементы ImGui. Он меняет то же состояние,
  из которого в текущем кадре формируются данные GPU.

Геометрические данные и математические тесты не требуют создания Vulkan device.
Новые тяжёлые зависимости не добавлены.

## Камера и преобразования

Мировая система — правосторонняя, Y направлен вверх. Камера смотрит вдоль
единичного вектора `forward`, вычисленного из yaw/pitch в градусах. View строится
через `lookAtRH(position, position + forward, up)`. Pitch ограничен диапазоном
[-89°,89°], чтобы направление не совпало с вертикалью. Начальная позиция
(0; 2,5; 9), yaw = -90°, pitch ≈ -15,52°: обе модели и маркеры попадают в кадр.

Perspective использует `perspectiveRH_ZO`, Orthographic — `orthoRH_ZO`.
Суффикс ZO означает глубину [0,1]. После построения Projection ровно один раз
выполняется `projection[1][1] *= -1`. В shader и viewport дополнительного
переворота нет. Near положителен, Far всегда больше Near. Отношение сторон
вычисляется по реальному framebuffer и области сцены, с учётом панели ImGui
шириной 384 UI-единицы и масштаба DPI. Начальная половина высоты ортографической
области — 3,2, чтобы обе модели помещались и после уменьшения ширины окна.

Model имеет порядок `T * Rx * Ry * Rz * S`. Анимация добавляет смещение и вращение
к базовому преобразованию. Масштаб ограничен положительными значениями [0,05;5],
поэтому Model остаётся обратимой и не меняет ориентацию граней. Фаза анимации
накапливается по delta time отдельно для каждой модели, в том числе когда она
не выбрана в интерфейсе. Pause сохраняет фазу, Reset Animation обнуляет её.

## Геометрия и нормали

В исходной топологии остаются 20 уникальных вершин и 12 пятиугольных граней
правильного додекаэдра. Координаты построены с золотым сечением
`φ = (1 + √5)/2`; длина ребра равна `2/φ`. Эта топология используется прежним
полным тестом замкнутости, выпуклости, рёбер, углов и площади.

Для GPU `makeFlatDodecahedron()` дублирует вершины по граням: 12 × 5 = 60.
Нормаль каждой грани вычисляется как
`normalize(cross(p1 - p0, p2 - p0))` и записывается всем пяти вершинам грани.
Наружная ориентация следует из исходного winding и проверяется тестами.
Каждая грань разбита веером на три треугольника: всего 36 треугольников,
108 индексов `uint32_t`. Оба экземпляра используют общие Vertex и Index Buffer.

Поле `color` сохранено для исторической геометрии и её тестов. Освещённая модель
его не использует: location 1 читает поле `normal`. Результирующие цвета
определяются материалами и расчётом света во фрагментном шейдере.

Для нормалей используется `transpose(inverse(Model))`. Vertex Shader умножает
нормаль на верхнюю матрицу 3×3, Fragment Shader нормализует результат. Это
сохраняет перпендикулярность нормали поверхности при неравномерном масштабе.
Поскольку у соседних граней разные вершины, интерполяция не сглаживает рёбра.

## Материал и Blinn–Phong

Материал каждого объекта хранит Albedo, Specular Color и Shininess [1;256].
Albedo определяет диффузный цвет поверхности, Specular Color — цвет блика,
Shininess — его концентрацию. Это независимые значения для каждой модели.

Во Fragment Shader вычисляются единичные векторы N (нормаль), L (к свету),
V (к камере) и H = normalize(L + V). Вклад источника:

```text
diffuse = max(dot(N,L), 0)
specular = pow(max(dot(N,H), 0), shininess), если diffuse > 0
direct = radiance * (albedo * diffuse + specularColor * specular)
ambient = ambientColor * ambientIntensity * albedo
result = ambient + directional + сумма point + сумма spot
```

Если N·L ≤ 0, функция возвращает ноль для обеих прямых составляющих:
блик не возникает на неосвещённой стороне. Безопасная нормализация защищает
случаи нулевых векторов, например совпадение позиции света с фрагментом.
Освещение суммируется в линейном пространстве, преобразование для sRGB
выполняет формат framebuffer. При очень больших интенсивностях цвет может
достичь максимума экранного диапазона; HDR и tone mapping в задание не входят.

### Источники света

Directional Light хранит направление распространения лучей; в формуле
используется `L = normalize(-direction)`. Его интенсивность не зависит от
расстояния. Цвет и интенсивность ambient редактируются отдельно.

Point Light хранит мировую позицию и RGB с интенсивностью. Для него
`delta = lightPosition - worldPosition` и
`attenuation = 1 / max(dot(delta,delta), 0.01)`. За пределами радиуса 0,1
действует закон обратных квадратов: удвоение расстояния уменьшает вклад
в четыре раза. Ограничение вблизи центра устраняет бесконечность.

Spot Light дополнительно хранит единичное направление и два **полуугла**
конуса в градусах. Сравнивается косинус между направлением лучей и направлением
от света к фрагменту. Внутри внутреннего конуса коэффициент равен 1, вне
внешнего — 0. Между ними используется `smoothstep(cosOuter, cosInner, cosine)`.
Точечное затухание и Blinn–Phong умножаются на этот коэффициент.

Углы ограничены [0°;89°], перепутанные значения сортируются. Совпавшие углы
дают жёсткую границу с явным сравнением без деления на ноль. Нулевое направление
заменяется на (0;-1;0). Нечисловые параметры света заменяются допустимыми.

## Descriptor Sets и память CPU/GPU

| Set | Binding | Тип | Данные | Стадии |
|---|---|---|---|---|
| 0 — Scene | 0 | Uniform Buffer, std140 | ViewProjection, позиция камеры, ambient, directional, количества света | vertex + fragment |
| 0 — Scene | 1 | Storage Buffer, std430 | `PointLight lights[]` переменной длины | fragment |
| 0 — Scene | 2 | Storage Buffer, std430 | `SpotLight lights[]` переменной длины | fragment |
| 1 — Object | 0 | Uniform Buffer, std140 | Model, NormalMatrix, Albedo, Specular/Shininess | vertex + fragment |

У Scene один набор и один UBO. У каждой модели — собственные Object UBO и
descriptor set. Всего три набора, три uniform buffer и два storage buffer.
Descriptor Pool резервирует ровно 3 UBO-дескриптора, 2 SSBO-дескриптора и 3 sets.
Pipeline Layout содержит оба layout. В `render()` Scene привязывается как set 0,
а перед каждой моделью привязывается её set 1.

Все поля GPU-структур состоят из `mat4`, `vec4`, `uvec4`; у структур указан
`alignas(16)`. `static_assert` проверяет размеры, выравнивание и смещения.
Дополнительно `verify_shader_layout.py` читает настоящий SPIR-V через
`spirv-dis` и сверяет offsets, strides, sets и bindings.

| Структура | Размер | Смещения полей в байтах |
|---|---:|---|
| SceneUniform | 144 | 0, 64, 80, 96, 112, 128 |
| ObjectUniform | 160 | 0, 64, 128, 144 |
| GpuPointLight | 32 | 0, 16; stride массива 32 |
| GpuSpotLight | 64 | 0, 16, 32, 48; stride массива 64 |

И UBO, и SSBO используют offset 0, поэтому дополнительное выравнивание смещения
между элементами отдельных UBO не требуется. Типы и декларации GLSL находятся
в `scene.glsl`, `object.glsl` и `dodecahedron.frag`.

### Динамические SSBO и синхронизация

Сохранён один кадр в полёте. `prepare()` ждёт fence предыдущего submit до
входа в `update()`. После этого CPU может менять mapped buffers, обновлять
дескрипторы и освобождать заменяемые буферы. `graphics::writeBuffer()` проверяет
размер записи и вызывает `vmaFlushAllocation`, в том числе для non-coherent памяти.

При нехватке ёмкости `ensureCapacity()` сначала выделяет новый буфер, затем
уничтожает старый и обновляет дескриптор. Ёмкость растёт геометрически;
ограничение `maxStorageBufferRange` учитывается до выделения. Уменьшение
количества света сохраняет буфер и меняет данные/счётчики. GLSL обходит
фактическое количество источников, а не ёмкость выделенной памяти.

При нуле источников остаётся буфер хотя бы на одну обнулённую запись.
Descriptor range ненулевой, count в UBO равен нулю, shader не читает массив.
SSBO действительно содержат runtime arrays; фиксированных массивов света в UBO нет.

Resize ждёт завершения очереди, пересоздаёт depth image, framebuffer и
семафоры завершения по новому количеству изображений swapchain. Если меняется
число изображений, переинициализируется Vulkan backend ImGui. При нулевом
размере framebuffer main ожидает события. После восстановления окно продолжает
рисование. Ошибки рендеринга обрабатываются с освобождением ресурсов; частичная
инициализация также имеет путь очистки.

## Визуализация света

Точечный свет изображается маленьким кубом в мировой позиции источника.
Прожектор имеет такой же маркер, цветную линию направления, две белые окружности
и образующие внутреннего/внешнего конусов. Геометрия строится из текущих
направления и углов; отдельный устойчивый базис позволяет смотреть вдоль Y.

Кубы рисует triangle-list pipeline, линии — line-list pipeline с толщиной 1,
без требования Vulkan `wideLines` или `fillModeNonSolid`. Оба используют
`gizmo.vert/.frag`: только ViewProjection и постоянный цвет, без Blinn–Phong.
Depth test остаётся включённым, поэтому модели могут закрывать маркеры и линии.
Длина направляющих конуса — параметр визуализации, а не радиус отсечения света.
Переключатель Show Light Gizmos отключает построение и рисование gizmo-геометрии,
сохраняя вклад источников в освещение. Воздушное рассеяние и объёмный туман
не моделируются: видны границы конуса и освещённая область поверхности.

## Интерфейс и демонстрация

Панель справа прокручивается. Все разделы раскрываются независимо.

1. В Camera поменяйте XYZ, Yaw/Pitch, FOV, Near/Far или Projection. Reset Camera
   возвращает начальные параметры. У каждой проекции свой параметр масштаба.
2. В Scene / Objects выберите Object 1 или Object 2. Измените XYZ, Rotation,
   Scale; Reset Transform вернёт начальное размещение только выбранной модели.
3. В Animation доступны Play/Pause, Reset Animation, Speed, Radius и Vertical amplitude.
4. В Material измените Albedo, Specular Color и Shininess. Более высокая
   Shininess даёт узкий блик. Для проверки normal matrix задайте разный Scale XYZ.
5. В Ambient Light и Directional Light независимо меняйте цвет и интенсивность.
6. В Point Lights нажмите Add Point Light, раскройте запись и измените Position,
   Color, Intensity. Remove Point удаляет одну запись, Clear Points — все.
7. В Spot Lights аналогично доступны Add/Remove/Clear, Ray direction, Inner/Outer angle.
   Для мягкой границы увеличьте Outer относительно Inner.
8. В Light Visualization включите Show Light Gizmos и настройте длину конуса.

Для демонстрации закона обратных квадратов выключите Directional, уменьшите
Ambient и перемещайте единственный Point Light. Для прожектора оставьте один
Spot Light и меняйте его углы/направление. При нулевом количестве всех источников
и нулевом Ambient модели становятся чёрными, что можно проверить на фоне сцены.

## Сборка и запуск на Windows

Нужны Vulkan-драйвер, Vulkan SDK с `glslc`, CMake ≥ 3.20, Git и компилятор C++20.
Для MinGW рекомендуется MSYS2 UCRT64 с GCC и `mingw32-make`. Для математических
тестов используется уже имеющийся GLM; Python нужен для проверки SPIR-V ABI.
`verify_images.py` дополнительно использует Pillow и NumPy.

Стандартный вариант из PowerShell:

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\<версия>'
$env:Path = "C:\msys64\ucrt64\bin;$env:VULKAN_SDK\Bin;$env:Path"
.\scripts\build.ps1
.\scripts\run.ps1
```

Скрипт ищет SDK также в `%LOCALAPPDATA%\VulkanSDK`. Он создаёт временный диск
V:, указывающий на папку проекта: установленный MinGW Makefiles не обрабатывает
пути с кириллицей. Исходники не копируются, сборка остаётся в `build-debug-v`.
Диск удаляется в `finally`. При занятом V: укажите свободную букву, например
`-DriveLetter W`, и запускайте `run.ps1 -Executable build-debug-w\vulkan-starter-app.exe`.
Сборки и скомпилированные шейдеры исключены из Git.

Зависимости FetchContent имеют прежние закреплённые версии. При первой сборке
они скачиваются через Git. Для повторного использования локальных исходников
зависимостей предусмотрен `-DependencyRoot <каталог _deps>`.

В текущем окружении использованы уже установленные SDK и зависимости первой
работы; файлы зависимостей не изменяются:

```powershell
$cache = Join-Path $env:LOCALAPPDATA 'VulkanLab1Workspace'
$env:Path = "$cache\.tools\mingw-make\ucrt64\bin;$env:Path"
.\scripts\build.ps1 -DependencyRoot "$cache\build-debug\_deps" -UiHooks
.\scripts\run.ps1
```

На пути без кириллицы также работают обычные команды:

```powershell
cmake --preset mingw-debug
cmake --build build-debug --parallel 6
ctest --test-dir build-debug --output-on-failure
.\scripts\run.ps1 -Executable build-debug\vulkan-starter-app.exe
```

Рабочим каталогом приложения должен быть корень проекта: shader binaries
читаются из `shaders/`. `run.ps1` выставляет его автоматически. Для Release:
`build.ps1 -Configuration Release`, затем
`run.ps1 -Executable build-release-v\vulkan-starter-app.exe`.

## Проверки

`build.ps1` компилирует все четыре GLSL-шейдера через `glslc`, собирает C++,
запускает CTest и при наличии `spirv-val` валидирует четыре бинарника SPIR-V.

- `dodecahedron-geometry` сохраняет полный тест первой работы: 20 уникальных
  точек, 12 правильных пятиугольников, 30 общих рёбер, winding, выпуклость и площадь.
- `application-controls` адаптирован к ObjectUniform и материалам; сохраняет
  проверки проекций, трансформаций, анимации, её независимости и сбросов.
- `camera-lighting-gizmos` проверяет View, пользовательские near/far, reset,
  нормали, inverse transpose, материалы, ограничения света, soft/hard cone,
  inverse square, добавление/удаление 128 источников и геометрию gizmos.
- `shader-memory-layout` сверяет SPIR-V с C++ ABI при наличии Python и spirv-dis.

Проверка графической части с Vulkan:

```powershell
.\scripts\verify-vulkan.ps1
```

Она проверяет загрузку DLL Validation Layer, обычное закрытие, resize,
minimize/restore и сохраняет изображение `docs/testing/lab2-default.png`.
Затем запускает `--smoke-test`: за 600 кадров изменяет количество источников
0 → 33 каждого типа → 0 → 1, углы и направление прожектора, материалы,
неравномерный масштаб, камеру, проекцию, gizmos и анимацию. Проверяется нулевой
счётчик ошибок и предупреждений после полного освобождения ресурсов.
Synchronization Validation включается в `tests/validation/vk_layer_settings.txt`.

Для проверки реальных элементов ImGui нужно собрать с `-UiHooks`:

```powershell
.\tests\verify_controls.ps1
.\tests\verify_lighting_controls.ps1
python .\tests\verify_images.py
```

Тесты взаимодействуют с окном мышью/клавиатурой; пока они идут, не следует
перехватывать фокус. Только в тестовой сборке observer записывает прямоугольники
контролов, значения CPU и копии загруженных uniform-данных в JSON. В обычной
сборке эта инструментализация отсутствует. Исторические имена JSON `mvp` и
`uploadedMvp` означают вычисленное `Scene.viewProjection * Object.model`;
в GPU Object UBO хранится Model, общий VP хранится отдельно в Scene UBO.

Фактические результаты и журналы находятся в `docs/testing/`. Итоговый статус
проверок и ограничения приведены в `docs/testing/RESULTS.md`.

## Изменённые и добавленные файлы

Изменены: `.gitignore`, `CMakeLists.txt`, `README.md`, `source/main.cpp`, `application.hpp/.cpp`,
`application_state.hpp/.cpp`, `dodecahedron.hpp/.cpp`, `graphics_internal.hpp/.cpp`,
два shader `dodecahedron.vert/.frag`, тесты `application_state_tests.cpp`,
`ui_observer.hpp/.cpp`, `verify_controls.ps1`, `verify_images.py`.

Добавлены: `source/lighting.hpp/.cpp`, `light_gizmos.hpp/.cpp`,
`application_ui.hpp/.cpp`, shader include `scene.glsl`, `object.glsl`,
`gizmo.vert/.frag`, `scripts/build.ps1`, `run.ps1`, `verify-vulkan.ps1`,
`tests/lighting_tests.cpp`, `verify_shader_layout.py`, `verify_lighting_controls.ps1`,
настройка Validation Layer, этот документ и результаты в `docs/testing`.

## Ограничения и принятые решения

Один кадр в полёте упрощает объяснение и проверку обновления динамических буферов;
несколько кадров в полёте потребовали бы отдельных ресурсов или отложенного
удаления. Количество источников ограничено памятью устройства и Vulkan
`maxStorageBufferRange`, фиксированного GLSL-лимита нет. Это учебная модель
Blinn–Phong без теней, текстурирования, объёмного рассеяния и HDR.

Тестирование выполняется на доступной Windows/MinGW/Vulkan конфигурации.
Переносимость на другие GPU, ОС и компиляторы требует отдельного запуска тестов.
Отсутствие ошибок Validation Layers в проверенных сценариях не является
доказательством отсутствия всех возможных ошибок драйвера или приложения.

Использованные справочные материалы: [Khronos: Shader Memory Layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html)
для правил std140/std430 и [Khronos: Swapchain Semaphore Reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
для синхронизации семафоров presentation по изображениям swapchain.
