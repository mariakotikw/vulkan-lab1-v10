# Лабораторная №1, вариант 10

Базовая часть и дополнительные задания **1–6** выполнены на C++20 и Vulkan.
В сцене одновременно отображаются два правильных додекаэдра; управление находится
в одном окне ImGui `Lab 1 Controls`.

Ветка `lab1-v10`. Стабильные этапы сохранены в Git: исправленный starter — `19a2084`,
базовая лабораторная — `36cbd01`, версия с заданиями 1–5 на оценку 4 — **`f830316`**.
Текущая версия включает задание №6 и опубликована в
[публичном репозитории](https://github.com/mariakotikw/vulkan-lab1-v10).

## Сборка и запуск

Для свежего клона нужны CMake, MinGW с поддержкой C++20, `mingw32-make` и Vulkan SDK.
Переменная `VULKAN_SDK` должна указывать на SDK; компилятор, Make и SDK `Bin` должны
быть доступны через `PATH`. Из корня проекта в PowerShell:

```powershell
$env:VK_LAYER_PATH = Join-Path $env:VULKAN_SDK 'Bin'
cmake --preset mingw-debug
cmake --build build-debug --parallel 4
.\build-debug\vulkan-starter-app.exe
```

В уже настроенном локальном Windows-окружении также доступны скрипты из `.tools/`.
Они не включены в GitHub и не требуются для приведённых выше команд:

```powershell
& .\.tools\build.ps1
& .\.tools\start.ps1
```

Исполняемый файл — `build-debug/vulkan-starter-app.exe`; рабочий каталог — корень
проекта, поскольку шейдеры загружаются из `shaders/*.spv`.
Сборка: CMake preset `mingw-debug`, MinGW/GCC 14.1.0, GNU Make 4.4.1,
Vulkan SDK **1.4.363.0**, `glslc`. SDK установлен вне проекта.

Локальный `environment.ps1` задаёт переменные только для текущего процесса и
использует ASCII junction, чтобы MinGW Make корректно работал с кириллицей в пути.
При клонировании на другой компьютер проще выбрать путь без кириллицы.

CMake загружает закреплённые версии GLFW, vk-bootstrap, VMA, ImGui и GLM через
FetchContent. Сохранённый `build-debug/` содержит их кэш и результаты сборки;
сборка шейдеров выполняется автоматически через `glslc`.
Подробные инструкции сборки и запуска находятся в [README.md](README.md).

## Управление и дополнительные задания

Верхние кнопки **Object 1 / Object 2** выбирают редактируемый объект.
Настройки Projection общие; Transform, Animation и Color принадлежат каждому объекту.

1. **Projection:** Perspective / Orthographic; FOV 15–90°, исходно 45°;
   Half height 1–8, исходно 2.5. Используются GLM `perspectiveRH_ZO` и `orthoRH_ZO`,
   near=0.1, far=100, глубина [0,1] и инверсия Y для Vulkan.
2. **Transform:** Position X/Y/Z, Rotation X/Y/Z в градусах, Scale X/Y/Z.
   Scale ограничен 0.05–5. Reset Transform возвращает выбранному объекту его
   собственные исходные position/rotation/scale.
3. **Animation:** Play/Pause, Reset Animation, Speed, Radius, Vertical amplitude.
   Фаза интегрируется как `phase += deltaTime * speed`. Pause сохраняет фазу;
   Play продолжает без скачка из-за времени паузы. Движение пространственное:
   `offset = (radius*sin(phase), amplitude*sin(2*phase), radius*(cos(phase)-1))`;
   добавляется вращение `(0.45,0.7,0.3)*phase` радиан. Анимации независимы и
   продолжают работать при выборе другого объекта.
4. **Color:** ColorEdit3 задаёт RGB-множитель из UBO, без пересоздания pipeline.
   Белый цвет сохраняет исходные procedural vertex colors.
5. **Procedural colors:** цвет каждой вершины вычисляется из её локальной позиции
   как `(position / phi + 1) * 0.5`, затем интерполируется по треугольникам.
6. **Несколько объектов:** два descriptor set ссылаются на два отдельных UBO.
   Для обоих объектов используются общие vertex/index buffers и один pipeline.

Начальные параметры:

| Объект | Position | Rotation, градусы | Scale | RGB-множитель |
|---|---|---|---|---|
| Object 1 | (-1.5, 0, 0) | (5, 10, 0) | (0.6, 0.6, 0.6) | (1.0, 0.55, 0.35) |
| Object 2 | (1.5, 0, 0) | (15, 35, 5) | (0.5, 0.5, 0.5) | (0.35, 0.65, 1.0) |

## Геометрия и Vulkan

Одна геометрия: **20 уникальных вершин, 12 пятиугольников, 36 треугольников,
108 индексов uint32_t**. Координаты строятся через `phi=(1+sqrt(5))/2`:
8 точек `(±1,±1,±1)` и по 4 точки `(0,±1/phi,±phi)`, `(±1/phi,±phi,0)`,
`(±phi,0,±1/phi)`. Каждая грань разбита веером на три треугольника с наружным обходом.

| Ресурс сцены додекаэдров | Количество |
|---|---:|
| Объекты | 2 |
| Descriptor sets | 2 |
| Uniform buffers / их VMA allocations | 2 / 2 |
| Vertex buffers | 1 |
| Index buffers | 1 |
| Graphics pipelines | 1 |
| Descriptor set layouts | 1 |
| Descriptor pools | 1 |
| Indexed draw calls на кадр | 2 |

Существующий backend ImGui отдельно управляет ресурсами интерфейса.
Общий pool имеет `maxSets=2` и два `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` descriptors.
`descriptor_sets[i]` записан с `object_uniform_buffers[i]`, offset=0, range=80.

UBO `TransformUniform`: std140, `mat4 mvp` по offset 0, `vec4 user_color` по offset 64,
размер 80, align 16; соответствие C++/GLSL проверено `static_assert`.
Общие view/projection вычисляются один раз, затем для каждого объекта:
`MVP[i] = projection * view * model[i]`.
Model: `T(position + animation offset) * Rx * Ry * Rz * S`.
Камера `(3.5,3.5,3.5)` смотрит в начало координат.

Viewport сцены расположен слева от панели ImGui. Aspect ratio вычисляется из его
актуальных размеров с учётом framebuffer и масштаба UI, поэтому resize не растягивает
фигуры. Pipeline, vertex buffer и index buffer привязываются один раз;
в `application::render()` внутри цикла выполняется отдельный
`vkCmdBindDescriptorSets(..., &descriptor_sets[i], ...)` перед `vkCmdDrawIndexed(108,...)`.

UBO записываются после ожидания fence в `prepare()`; видимость mapped writes
обеспечивает `vmaFlushAllocation`. При shutdown выполняется `vkQueueWaitIdle`,
уничтожаются pipeline, descriptor pool/layout и оба UBO с allocations, затем общие
index/vertex buffers. Уничтожение pool освобождает оба set.

## Какие файлы нужны

- `source/`: приложение, состояние/математика объектов, геометрия, buffer helper
  и исходный Vulkan starter; `.hpp` задают интерфейсы соответствующих `.cpp`.
- `shaders/`: исходники GLSL; `.spv` генерируются при сборке и нужны для запуска.
- `tests/`: проверки геометрии и состояния, два файла тестового наблюдателя,
  Windows UI-тест и проверка изображений. Наблюдатель включается только через
  `LAB1_ENABLE_UI_TEST_HOOKS=ON`; обычная сборка использует OFF.
- `CMakeLists.txt`, `CMakePresets.json`: сборка; `README.md`: инструкция проекта;
  `LICENSE`, `NOTICE`: лицензия и авторство.
- `.tools/`: локальное окружение, Make, сборка/запуск/проверка, настройки Validation,
  актуальные результаты и архив. Каталог исключён из Git.
- `build-debug/`: автоматически созданный кэш и бинарные файлы, исключённые из Git.

## Проверки

Выполнено на Windows / Intel UHD Graphics, Vulkan SDK 1.4.363.0:

- **CTest 2/2 PASS**: правильность геометрии и procedural colors; std140;
  проекции/depth/aspect; XYZ transform/reset; animation timing/pause/resume/FPS;
  независимые per-object MVP/color и временные шкалы.
- **UI-тест 62/62 PASS**: настоящие клики и ввод в ImGui; разные handles двух
  descriptor sets и UBO; содержимое mapped UBO; два draw по 108 индексов;
  общий pipeline/VB/IB; оба объекта, проекции, resize и minimize/restore.
- **Проверка screenshots PASS**: две отдельные фигуры в обеих проекциях и после
  resize до 1000×680; изменение scale/color одного не меняет пиксели другого;
  различные интерполированные RGB на обеих поверхностях.
- **Обычный запуск:** 8 секунд рендеринга, resize 1000×680 → 1280×720,
  minimize/restore, штатное закрытие. Khronos Core + Synchronization Validation:
  **0 ERROR, 0 WARNING, exit code 0**, включая shutdown.

Повторение CTest после сборки:

```powershell
ctest --test-dir build-debug --output-on-failure
```

В настроенном локальном окружении обычный запуск проверяется через `.tools/verify.ps1`.
Полный UI-тест использует локальную `.tools/`, требует обычного рабочего стола Windows
и NumPy/Pillow для сравнения изображений; он наблюдает состояние и команды, не изменяя
логику приложения:

```powershell
. .\.tools\environment.ps1
cmake --preset mingw-debug -DFETCHCONTENT_UPDATES_DISCONNECTED=ON -DLAB1_ENABLE_UI_TEST_HOOKS=ON
cmake --build build-debug --parallel 4
& .\tests\verify_controls.ps1
python .\tests\verify_images.py
cmake --preset mingw-debug -DLAB1_ENABLE_UI_TEST_HOOKS=OFF
cmake --build build-debug --parallel 4
```

Локальные отчёты находятся в `.tools/logs/task6/`: `result.json`, `checks.json`,
`snapshots.json`, `image-checks.json`, `final-normal-result.json` и соответствующие логи.
[Итоговый screenshot двух объектов и ImGui](docs/screenshots/perspective.png).
Локальный Orthographic screenshot: `.tools/logs/task6/orthographic.png`.

## Очистка рабочего каталога

Старые диагностические запуски, прежние отчёты, резервные копии и состояние исходников
до уборки сохранены локально в `.tools/archive/before-cleanup-20261004.zip`.
Архив не включён в публичный репозиторий.
Все 202 файла архива проверены по SHA-256; внутри есть `cleanup-manifest.json`
с исходными путями, размерами, hashes и состоянием Git.
Установщик SDK удалён из проекта после установки; SDK и рабочие инструменты сохранены.
Исторические отдельные отчёты объединены в этот файл. Исходники и тесты не удалялись.
