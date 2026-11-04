# Лабораторная работа №3 — «Текстурирование»

**Вариант 10: правильный додекаэдр.** Проект продолжает лабораторную №2 на C++20, Vulkan, GLFW, GLM, Dear ImGui и Vulkan Memory Allocator (VMA).

Рабочая ветка: [`lab3-v10`](https://github.com/mariakotikw/vulkan-lab1-v10/tree/lab3-v10). История предыдущих работ описана в [LAB1.md](LAB1.md) и [LAB2.md](LAB2.md).

## 1. Цель и результат

Цель работы — загрузить изображения с диска, передать их в память устройства и применить к освещаемым трёхмерным объектам. Помимо базового текстурирования реализованы три дополнительных задания: выбор нескольких материалов, динамическое сэмплирование через Push Constants, Specular и Emissive Maps.

В сцене находятся два правильных додекаэдра. У первого по умолчанию выбран **Cosmic Core**, у второго — **Holographic Prism** с анимированным эффектом **Holographic Interference**. Материал любого объекта можно заменить независимо от другого.

Из лабораторной №2 сохранены камера, перспективная и ортографическая проекции, независимые трансформации и пространственная анимация объектов, плоские нормали, Blinn–Phong, Ambient/Directional/Point/Spot Lights и трёхмерная визуализация источников света. Функции геометрической анимации `Animation::offset()` и `Animation::rotation()` сохраняют прежнюю траекторию и вращение.

Изменения в организации данных:

- Вершина дополнена двумерными UV-координатами.
- Постоянные Vertex/Index Buffers загружаются через staging в device-local память.
- Из PNG создаются Vulkan Image, ImageView и Sampler.
- Отдельные объектные UBO заменены одним Dynamic Uniform Buffer.
- Каждый материал имеет свой Set 1 с тремя текстурами и ссылкой на общий объектный буфер.
- Параметры текстурного эффекта передаются через 48-байтный блок Push Constants.

## 2. Файлы и назначение модулей

| Файл или каталог | Назначение |
| --- | --- |
| `source/dodecahedron.hpp/.cpp` | Правильный додекаэдр, 60 записей вершин с плоскими нормалями и локальными UV граней, 108 индексов |
| `source/texture_material.hpp/.cpp` | Пресеты материалов, настройки карт и эффекта, CPU-структура Push Constants, расчёт stride и dynamic offsets |
| `source/image_data.hpp/.cpp` | Чтение PNG через stb_image и декодирование в плотно упакованные RGBA8-пиксели; модуль не зависит от Vulkan |
| `source/texture.hpp/.cpp` | Создание и уничтожение Vulkan-текстур, staging-копирование и переходы layout |
| `source/graphics.hpp/.cpp` | Mapped-буферы, запись по смещению, device-local буферы, одноразовая отправка upload-команд |
| `source/application.cpp` | Загрузка материалов, общий Dynamic UBO, Descriptor Sets, Push Constants и команды отрисовки |
| `source/application_state.hpp/.cpp` | Независимое состояние объектов, камеры, анимации, освещения и выбранных материалов |
| `source/application_ui.cpp` | Панель ImGui, выбор материала и регулировка эффекта |
| `shaders/dodecahedron.vert` | Преобразование позиции и нормали, передача UV фрагментному шейдеру |
| `shaders/dodecahedron.frag` | Blinn–Phong с Albedo/Specular/Emissive Maps |
| `shaders/texture_effect.glsl` | Sampler bindings, Push Constants и Holographic Interference |
| `shaders/scene.glsl`, `shaders/object.glsl` | Общий интерфейс Scene/Object для CPU и GLSL |
| `assets/textures/` | Готовые PNG, которые приложение действительно читает с диска |
| `scripts/generate_textures.py` | Воспроизводимая генерация оригинальных PNG через NumPy/Pillow |
| `third_party/stb_image.h`, `third_party/README.md` | Декодер, сведения об официальном источнике, фиксированной версии и лицензии |
| `scripts/` | Сборка, запуск, генерация текстур и проверки Vulkan |
| `tests/` | Проверки геометрии, состояния, света, UV, выравнивания, PNG и SPIR-V ABI |
| `docs/lab3/testing/` | Фактические результаты проверок лабораторной №3 |

## 3. UV-развёртка правильного додекаэдра

У додекаэдра 12 пятиугольных граней. Для flat shading каждая грань уже имеет пять собственных записей вершин: всего `12 × 5 = 60`. Общие геометрические углы намеренно дублируются, чтобы соседние грани могли иметь разные нормали и UV.

`makeFlatDodecahedron()` сохраняет исходную форму и порядок треугольников. Каждый пятиугольник разбивается веером на три треугольника: `12 × 3 × 3 = 108` индексов.

UV назначаются в собственной плоскости каждой грани:

1. Вычисляются центр грани и её наружная нормаль.
2. Ось Y объекта проецируется на плоскость грани и нормализуется в `up`. Если проекция вырождена, последовательно пробуются Z и X. Вторая ось — `right = normalize(cross(up, normal))`.
3. Смещения вершин относительно центра проецируются на две оси этого базиса.
4. Координаты переводятся в квадрат текстуры с центром `(0.5, 0.5)` и одинаковым масштабом по обеим осям.
5. Направление V согласуется с порядком строк PNG: декодер сохраняет верхнюю строку изображения первой.

Используется `scale = 0.46 / radius`, где radius — максимальное расстояние вершины до центра грани, и формула `uv = 0.5 + scale × (dot(local, right), -dot(local, up))`. UV лежат внутри `[0.04, 0.96]`, то есть сохраняется небольшой запас до границы изображения.

Одинаковый масштаб по U и V сохраняет форму рисунка. Текстура повторяется на каждой из 12 граней. Это локальная развёртка граней, поэтому рисунок не обязан непрерывно переходить через рёбра. Направление рисунка определяется геометрией, а не случайным порядком вершин.

Вершинный атрибут `location = 2` имеет формат `VK_FORMAT_R32G32_SFLOAT` и смещение `offsetof(geometry::Vertex, uv)`. `dodecahedron.vert` передаёт его в `texCoord`; интерполяцию внутри треугольника выполняет GPU.

## 4. Художественные материалы и PNG

| Пресет | Основной рисунок | Набор карт |
| --- | --- | --- |
| **Cosmic Core** | Тёмные сине-фиолетовые туманности, звёздная пыль и яркие энергетические области | Albedo, Specular, Emissive |
| **Nebula Mist** | Более светлый вариант космической туманности | Albedo, Specular, Emissive |
| **Holographic Prism** | Радужные переливы, тонкие полосы и цифровой узор | Albedo, Specular, Emissive |
| **Prism Grid** | Голографическая геометрическая сетка | Только Albedo; дополнительные карты заменены чёрными |

Изображения — оригинальные процедурные материалы. Готовые PNG хранятся в репозитории, а генератор нужен для их воспроизводимого обновления. Во время запуска приложение загружает файлы из `assets/textures/`; рисунок не подменяется вычислением цвета непосредственно во фрагментном шейдере.

Имена файлов связаны с пресетами: `cosmic_core_*`, `nebula_mist_*`, `holographic_prism_*` и `prism_grid_albedo.png`. Три полных набора и один Albedo-only набор составляют **10 PNG**. Размер каждой карты — **512 × 512** пикселей.

Генератор сочетает периодический многомасштабный шум на основе FFT, аналитические волны и маски, растеризацию линий и Gaussian blur. Космические карты используют связанные маски облаков, звёзд и разветвлённых энергетических прожилок. Голографические карты сочетают ограниченную радужную палитру, интерференционные полосы, треугольные участки, решётку и редкие цифровые дорожки. Specular и Emissive строятся из тех же полей, поэтому их детали совпадают с рисунком Albedo.

Для повторной генерации нужны Python 3.10+, NumPy и Pillow:

```powershell
python -m pip install numpy pillow
python scripts/generate_textures.py
```

Базовый seed равен `31010`; у пресетов независимые потоки генератора случайных чисел. При неизменном скрипте и окружении генерация воспроизводима. Происхождение изображений и дополнительные команды приведены в [assets/textures/README.md](assets/textures/README.md).

Пресеты задают текстурные ресурсы. Выбор другого пресета не сбрасывает положение, поворот, масштаб, геометрическую анимацию, камеру и настройки другого объекта.

## 5. Декодирование изображения и цветовое пространство

`graphics::loadImageRgba(path)` использует официальный **stb_image v2.30**. Декодер запрашивает четыре канала независимо от исходного формата PNG и возвращает `ImageData { width, height, pixels }`. Размер массива — `width × height × 4` байт. Порядок строк сохраняется без вертикального переворота.

В этой сборке включён только PNG-декодер. На Windows поддержаны пути UTF-8; максимальный размер стороны при декодировании ограничен 8192 пикселями. Пустой путь, отсутствующий файл или ошибочные данные приводят к исключению с описанием. Выделенная stb_image память освобождается и при исключениях.

Формат GPU зависит от смысла карты:

| Карта | Формат Image | Причина |
| --- | --- | --- |
| Albedo | `VK_FORMAT_R8G8B8A8_SRGB` | Цвет PNG переводится из sRGB в линейное пространство при сэмплировании |
| Emissive | `VK_FORMAT_R8G8B8A8_SRGB` | Цвет свечения также декодируется в линейное пространство |
| Specular | `VK_FORMAT_R8G8B8A8_UNORM` | Карта задаёт коэффициенты отражения; дополнительная гамма-коррекция им не нужна |

CPU не выполняет ручную гамма-коррекцию пикселей перед копированием. Преобразование цветовых карт обеспечивает выбранный sRGB-формат изображения.

## 6. Vulkan-текстуры и staging

`graphics::Texture` хранит `VkImage`, `VmaAllocation`, `VkImageView`, `VkSampler`, размеры, формат и фактические флаги памяти. `loadTexture(path, srgb)` сначала декодирует PNG, затем создаёт GPU-ресурсы.

Последовательность загрузки:

1. Создать host-visible staging buffer с `VK_BUFFER_USAGE_TRANSFER_SRC_BIT`.
2. Скопировать RGBA8-пиксели в mapped память staging и выполнить `vmaFlushAllocation()`.
3. Создать 2D Image с optimal tiling и usage `TRANSFER_DST | SAMPLED`.
4. Выделить память через VMA с `VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE` и обязательным `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`.
5. Перевести layout, скопировать staging в Image и подготовить Image к чтению шейдером.
6. Отправить upload-команды в graphics queue, дождаться fence и только затем освободить staging.
7. Создать ImageView и Sampler.

Все операции идут через ту же graphics queue, что и отрисовка. Переноса владения между семействами очередей нет; оба queue family index в барьерах равны `VK_QUEUE_FAMILY_IGNORED`.

Точные переходы изображения:

| Переход | Этапы pipeline | Access masks |
| --- | --- | --- |
| `UNDEFINED → TRANSFER_DST_OPTIMAL` | `TOP_OF_PIPE → TRANSFER` | `0 → TRANSFER_WRITE` |
| `vkCmdCopyBufferToImage()` | Copy в mip 0, слой 0, весь размер PNG | Плотно упакованные строки RGBA8 |
| `TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` | `TRANSFER → FRAGMENT_SHADER` | `TRANSFER_WRITE → SHADER_READ` |

Sampler использует линейную фильтрацию и `REPEAT` по U/V/W. Создаётся один mip-уровень, LOD ограничен нулём. Анизотропная фильтрация выключена, поскольку соответствующая возможность логического устройства не запрашивается.

Перед созданием проверяются `maxImageDimension2D` и возможности формата: sampled image, linear filtering и transfer destination. После выделения памяти `vmaGetAllocationMemoryProperties()` подтверждает наличие `DEVICE_LOCAL`.

## 7. Vertex/Index Buffers в памяти устройства

Постоянная геометрия загружается через `graphics::createDeviceLocalBuffer(data, size, usage)`:

```text
CPU mesh → mapped staging (TRANSFER_SRC)
         → vkCmdCopyBuffer()
         → device-local Vertex/Index Buffer (TRANSFER_DST | final usage)
```

Конечная память запрашивается с `AUTO_PREFER_DEVICE` и обязательным `DEVICE_LOCAL`. Для неё не запрашиваются mapping и host access; поле `mapped` остаётся `nullptr`. CPU пишет только в staging.

После копирования выполняется `VkBufferMemoryBarrier`:

- источник: stage `TRANSFER`, access `TRANSFER_WRITE`;
- получатель для вершин: stage `VERTEX_INPUT`, access `VERTEX_ATTRIBUTE_READ`;
- получатель для индексов: stage `VERTEX_INPUT`, access `INDEX_READ`.

`immediateSubmit()` создаёт transient command pool, первичный command buffer с `ONE_TIME_SUBMIT`, fence, отправляет команды и ждёт завершения. Затем освобождаются временные command resources и staging. Если создание ресурса или запись команд завершились исключением, уже созданные объекты также освобождаются.

В журнале выводятся размер, usage и фактические `memoryFlags` конечных буферов. На интегрированной Intel GPU память с `DEVICE_LOCAL` может одновременно быть `HOST_VISIBLE`: это нормальная модель общей памяти UMA. Требование работы состоит в выборе device-local типа и загрузке через staging; отдельная физическая микросхема VRAM на таком устройстве не появляется.

Динамические UBO, SSBO света и постоянно обновляемая геометрия маркеров остаются host-visible. Их назначение отличается от неизменяемых вершин и индексов додекаэдров.

## 8. Один Dynamic Uniform Buffer

`ObjectUniform` сохраняет прежний состав и std140-совместимый размер **160 байт**:

| Поле | Смещение | Размер |
| --- | ---: | ---: |
| `model` | 0 | 64 |
| `normal_matrix` | 64 | 64 |
| `albedo` | 128 | 16 |
| `specular_shininess` | 144 | 16 |

Устройство сообщает `minUniformBufferOffsetAlignment = A`. Выровненный шаг записи:

```text
S = sizeof(ObjectUniform) = 160
stride = ceil(S / A) × A
offset(i) = i × stride
bufferSize = object_count × stride
```

Например, при `A = 256` stride равен 256, а два объекта используют смещения 0 и 256. Это пример расчёта; фактические значения устройства выводятся при запуске.

Общий `object_uniform_buffer` создаётся один раз. При обновлении `writeBuffer(..., sizeof(ObjectUniform), offset(i))` записывает состояние каждого объекта в его участок. Padding между записями не входит в структуру и не читается шейдером.

Каждый материал содержит дескриптор `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`, указывающий на один и тот же буфер. У `VkDescriptorBufferInfo` базовый offset равен 0, а range — ровно 160 байт. Перед каждым `vkCmdDrawIndexed()` передаётся один dynamic offset соответствующего объекта.

Range описывает одну запись, поэтому весь общий буфер не обязан помещаться в `maxUniformBufferRange`; это ограничение проверяется для `sizeof(ObjectUniform)`. Смещение должно быть выровнено, помещаться в `uint32_t` и вместе с range оставаться в пределах общего буфера. Вычисления stride/offset выполняются отдельными проверяемыми CPU-функциями.

Параметры текстурного эффекта не добавляются в Object UBO.

## 9. Descriptor Sets

| Set | Binding | Тип | Содержимое |
| ---: | ---: | --- | --- |
| 0 — Scene | 0 | Uniform Buffer | Матрица камеры, позиция, ambient/directional light, количества источников |
| 0 — Scene | 1 | Storage Buffer | Динамический массив Point Lights |
| 0 — Scene | 2 | Storage Buffer | Динамический массив Spot Lights |
| 1 — Material/Object | 0 | Dynamic Uniform Buffer | Общий Object UBO; запись выбирает dynamic offset |
| 1 — Material/Object | 1 | Combined Image Sampler | Albedo Map |
| 1 — Material/Object | 2 | Combined Image Sampler | Specular Map |
| 1 — Material/Object | 3 | Combined Image Sampler | Emissive Map |

Для четырёх материалов выделены четыре Set 1. При выборе материала меняется привязываемый descriptor set, а dynamic offset продолжает указывать на запись именно рисуемого объекта. Даже если оба объекта выбрали один пресет, они сохраняют независимые трансформации и параметры материала.

Все текстуры загружаются при инициализации. Переключение пресета в интерфейсе не создаёт и не уничтожает GPU-текстуры.

`Prism Grid` не содержит Specular/Emissive файлов. Для bindings 2 и 3 используется одна общая текстура **1 × 1, RGBA = (0, 0, 0, 255)**. Поэтому все bindings остаются валидными, а отсутствующая карта даёт нулевой вклад. Общий fallback уничтожается отдельно от ресурсов пресетов.

## 10. Push Constants и Holographic Interference

`TexturePushConstants` содержит три `vec4` с выравниванием 16 и общим размером **48 байт**. В C++ и GLSL используются одинаковые смещения:

| Вектор | Смещение | x | y | z | w |
| --- | ---: | --- | --- | --- | --- |
| `animation` | 0 | Время | Скорость | UV distortion | Blend factor |
| `controls` | 16 | Эффект включён | Color modulation | Albedo включена | Specular включена |
| `emission` | 32 | Emissive включена | Сила emissive | Резерв | Резерв |

Флаги представлены числами 0/1 во float-полях. Это исключает различия представления C++ `bool` и GLSL. Pipeline layout содержит один push range для `VK_SHADER_STAGE_FRAGMENT_BIT`; размер сравнивается с `maxPushConstantsSize` устройства.

Перед рисованием каждого объекта `vkCmdPushConstants()` устанавливает его текущие значения. Поэтому настройки и включение эффекта независимы у двух объектов.

Алгоритм `sampleAlbedo()` в `texture_effect.glsl`:

1. Прочитать Albedo по исходным UV.
2. Если эффект выключен, вернуть эту обычную выборку.
3. Рассчитать фазу `time × speed`.
4. Повернуть вторые координаты относительно `(0.5, 0.5)`, добавить небольшой плавный дрейф и синусоидальное искажение.
5. Второй раз прочитать **ту же Albedo Map**, но по преобразованным UV.
6. Смешать две выборки с `blend factor`.
7. Применить мягкую радужную модуляцию с регулируемой силой.

Это две реальные texture samples с разными координатами, а не один вызов `texture()` с перекрашиванием. Изменение времени обеспечивает движение полос и переливы при неподвижной геометрии.

Источник времени — время кадра, получаемое из `glfwGetTime()`. Оно не связано с `Animation::phase` или флагом `Animation::playing`: нажатие **Pause** останавливает геометрию, а текстурный эффект продолжает работать. Выключение **Holographic Interference** возвращает обычное текстурирование. Выключение **Albedo Map** возвращает единичный множитель для основного цвета.

## 11. Три карты и Blinn–Phong

Сохраняется расчёт Ambient, Diffuse и Specular для Directional, Point и Spot Lights:

```text
surfaceAlbedo   = objectAlbedo × sampledAlbedo
surfaceSpecular = objectSpecularColor × sampledSpecular
color = ambient × surfaceAlbedo
      + Σ BlinnPhong(N, V, L, radiance, surfaceAlbedo, surfaceSpecular)
      + sampledEmissive × emissiveStrength
```

Для точечных источников и прожекторов сохраняется затухание по обратному квадрату расстояния. У Spot Lights остаётся мягкая граница между inner/outer angles.

Albedo участвует в фоновой и диффузной составляющих. Specular Map изменяет локальный коэффициент зеркального отражения и влияет именно на блик; его степень по-прежнему определяется `Shininess`. При выключении **Specular Map** зеркальная составляющая объекта становится нулевой.

Emissive прибавляется после освещения. Она остаётся видимой при нулевой интенсивности ambient/directional и пустых массивах point/spot. Выключение **Emissive Map** или установка **Emission = 0** убирает этот вклад.

Emissive не создаёт дополнительного источника света для соседних объектов. Bloom и размытие свечения за контур модели не реализуются.

## 12. Интерфейс

В правой панели сначала выбирается **Object 1** или **Object 2**. Следующие разделы редактируют выбранный объект:

- **Material**: цвет Albedo, Specular Color, Shininess.
- **Textures / Materials**: имя текущего материала, список **Preset**, переключатели трёх карт и сила **Emission**.
- **Holographic Effect**: включение эффекта, **Animation Speed**, **UV Distortion**, **Blend Factor**, **Color Modulation**.

Сохранены разделы **Camera**, **Scene / Objects**, **Animation**, **Ambient Light**, **Directional Light**, **Point Lights**, **Spot Lights** и **Light Visualization**. Камера и освещение относятся ко всей сцене; трансформации, геометрическая анимация, материалы и текстурные эффекты принадлежат отдельным объектам.

Все изменения применяются в текущем цикле обновления без пересоздания pipeline.

## 13. Сборка и запуск на Windows

Нужны CMake, MinGW UCRT64 (`g++`, `mingw32-make`) и Vulkan SDK с `glslc`. Для графических проверок необходимы Vulkan-драйвер и Validation Layers. Python используется отдельными проверками и генератором текстур; для обычного запуска приложения генератор не нужен, поскольку PNG уже находятся в репозитории.

Запускать команды следует из корня проекта. Скрипт `build.ps1` временно создаёт диск через `subst`, чтобы MinGW Makefiles работали с исходным путём, содержащим кириллицу. Буква V должна быть свободна; при необходимости можно передать `-DriveLetter W` и использовать соответствующий каталог сборки.

### PowerShell

При готовом компиляторе, make и Vulkan SDK в окружении:

```powershell
.\scripts\build.ps1
.\scripts\run.ps1
```

Для использования ранее установленных зависимостей на этой машине:

```powershell
$cache = Join-Path $env:LOCALAPPDATA 'VulkanLab1Workspace'
$env:Path = "$cache\.tools\mingw-make\ucrt64\bin;$env:Path"
.\scripts\build.ps1 -DependencyRoot "$cache\build-debug\_deps"
.\scripts\run.ps1
```

`-DependencyRoot` указывает на каталог с исходниками GLFW, GLM, ImGui, vk-bootstrap и VMA. Если параметр не задан, CMake получает зависимости через FetchContent.

### Git Bash

PowerShell-скрипт вызывается через `powershell.exe`; запись `.\scripts\run.ps1` не является командой Bash.

```bash
powershell.exe -NoProfile -Command '
$cache = Join-Path $env:LOCALAPPDATA "VulkanLab1Workspace"
$env:Path = "$cache\.tools\mingw-make\ucrt64\bin;$env:Path"
& ./scripts/build.ps1 -DependencyRoot "$cache\build-debug\_deps"
'
powershell.exe -NoProfile -File ./scripts/run.ps1
```

Если кэш зависимостей не используется и make уже доступен в PATH:

```bash
powershell.exe -NoProfile -File ./scripts/build.ps1
```

Сборка компилирует GLSL в SPIR-V и запускает CTest. Если `spirv-val` найден, скрипт дополнительно проверяет все четыре SPIR-V модуля. По умолчанию приложение собирается в `build-debug-v/vulkan-starter-app.exe`.

`run.ps1` устанавливает рабочий каталог в корень проекта. Поэтому относительные пути `shaders/` и `assets/textures/` одинаково работают независимо от текущего каталога терминала. Абсолютные пути к изображениям конкретного компьютера в код не зашиты.

Smoke-запуск и проверка Vulkan:

```powershell
.\scripts\run.ps1 -SmokeTest
.\scripts\verify-vulkan.ps1
```

В Git Bash:

```bash
powershell.exe -NoProfile -File ./scripts/run.ps1 -SmokeTest
powershell.exe -NoProfile -File ./scripts/verify-vulkan.ps1
```

## 14. Проверки и их фактические результаты

Окончательные результаты сборки, CTest, графических сценариев, Validation Layers и полученные изображения фиксируются в [docs/lab3/testing/RESULTS.md](docs/lab3/testing/RESULTS.md). Только этот отчёт с журналами определяет, какие проверки действительно были выполнены и в каком окружении. Описание проверок ниже не является утверждением об их успешном прохождении.

| Проверка | Что проверяется |
| --- | --- |
| `dodecahedron-geometry` | Количество вершин/индексов, правильность геометрии и нормалей |
| `application-controls` | Состояние и независимость объектов, камера и геометрическая анимация |
| `camera-lighting-gizmos` | Освещение и геометрия маркеров |
| `uv-dynamic-ubo-push-constants` | UV, stride/offsets, параметры материалов и Push Constants |
| `png-file-decoding` | Декодирование реальных PNG в RGBA8, размеры, непустой рисунок и отказ на ошибочных файлах |
| `shader-memory-layout` | Смещения полей Scene/Object/TexturePush, sampler bindings, SSBO strides и несколько image samples в SPIR-V |
| `scripts/verify-vulkan.ps1` | Реальный запуск, Validation Layers, resize/minimize/restore и smoke-сценарий |

В коде сохраняется один кадр в полёте. `prepare()` ждёт его fence до обновления Dynamic UBO, SSBO, mapped-маркеров и дескрипторов. Это предотвращает запись CPU в ресурсы, которые ещё читает GPU. При загрузке staging действует отдельный fence.

При завершении сначала ожидается graphics queue, затем уничтожаются pipeline, descriptor pool/layouts, текстурные sampler/view/image и буферы. Ошибка инициализации также проходит через освобождение уже созданных ресурсов.

## 15. Соответствие требованиям и показ кода

| Требование | Где смотреть |
| --- | --- |
| Настоящие изображения из файловой системы | `assets/textures/`, `graphics::loadImageRgba()`, `graphics::loadTexture()` |
| UV каждой пятиугольной грани | `geometry::makeFlatDodecahedron()` |
| Vulkan Image/View/Sampler | `graphics::Texture`, `uploadRgba()`, `destroyTexture()` в `texture.cpp` |
| Staging и device-local геометрия | `graphics::createDeviceLocalBuffer()`, `immediateSubmit()` |
| Один Dynamic UBO | `object_uniform_buffer`, его создание и запись в `application.cpp` |
| Выравнивание и dynamic offsets | `dynamicUniformStride()`, `dynamicUniformOffset()`, bind перед каждым draw |
| Доп. №1: несколько материалов | `materialPresets()`, `texture_materials`, `createDescriptors()`, Preset в UI |
| Доп. №2: динамическое сэмплирование | `TexturePushConstants`, `TextureSettings::pushConstants()`, `sampleAlbedo()`, `vkCmdPushConstants()` |
| Доп. №3: Specular/Emissive | `dodecahedron.frag`, bindings 2/3 в `createDescriptors()`, `createBlackTexture()` |
| Сохранение функциональности №2 | `application_state.cpp`, `lighting.cpp`, `light_gizmos.cpp`, сохранённые разделы UI |
| Согласование CPU/GLSL | static assertions в заголовках и `tests/verify_shader_layout.py` |

Порядок демонстрации на защите:

1. Запустить сцену с Cosmic Core слева и Holographic Prism справа. Показать разные рисунки и плоские грани.
2. Выбрать Object 1 и изменить его положение/поворот/масштаб. Показать, что Object 2 не меняется.
3. Переключить материал Object 1 через все пресеты. Убедиться, что трансформации сохранились. Выбрать Prism Grid и объяснить чёрные fallback-карты.
4. Выбрать Object 2. При остановленной геометрической анимации показать движение голографического рисунка. Отключить эффект и сравнить обычное текстурирование.
5. Изменить скорость, distortion, blend и color modulation. Показать соответствующий 48-байтный push block в C++ и GLSL.
6. Сравнить блик с включённой и выключенной Specular Map; при необходимости изменить Shininess и направление света.
7. Обнулить Ambient и Directional intensity, очистить Point/Spot Lights. Показать оставшиеся emissive-участки и их исчезновение при выключении Emissive Map.
8. Восстановить свет, добавить Point/Spot, включить Show Light Gizmos. Показать сохранение освещения и визуализации из №2.
9. В коде пройти цепочку PNG → RGBA → staging → Image → sampler; затем показать staging Vertex/Index Buffers и журнал флагов памяти.
10. Показать один Object UBO, выровненный stride, два dynamic offsets и четыре material sets с одним и тем же buffer handle. Завершить отчётом фактически выполненных проверок.

## 16. Границы реализации

Текстуры используют один mip-уровень и линейную фильтрацию. Mip generation и anisotropy не включены. На очень мелких или удалённых деталях это может дать алиасинг.

Holographic Interference — анимированное сэмплирование и цветовая модуляция непрозрачной поверхности. Физическая иридесценция, прозрачность, Bloom/HDR, deferred rendering, offscreen-проходы и Dynamic Rendering не входят в эту реализацию. Существующая схема render pass сохранена.

Загрузка ресурсов синхронна и выполняется при инициализации. Все пресеты заранее находятся в памяти; изменение выбора материала не запускает новую загрузку.

Фактическая поддержка GPU, компилятора и конфигурации определяется итоговым отчётом проверок. Работоспособность на непроверенных платформах и драйверах отдельно не утверждается.
