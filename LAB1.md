# Лабораторная работа №1, вариант 10

Правильный додекаэдр на C++20 и Vulkan с дополнительными заданиями 1–5.
Дополнительное задание №6 не реализовано. Управление находится в одном окне `Lab 1 Controls`.
Стабильный starter с исправленной валидацией сохранён коммитом `19a2084` в ветке `lab1-v10`.
Базовая лабораторная сохранена отдельным коммитом `36cbd01` перед дополнительными заданиями.
Описание управления, анимации и автоматических проверок: [LAB1_EXTRAS.md](LAB1_EXTRAS.md).

## Геометрия

`source/dodecahedron.cpp` строит 20 уникальных вершин через `phi = (1 + sqrt(5)) / 2`:

- 8 вершин `(±1, ±1, ±1)`;
- 4 вершины `(0, ±1/phi, ±phi)`;
- 4 вершины `(±1/phi, ±phi, 0)`;
- 4 вершины `(±phi, 0, ±1/phi)`.

Для этих координат задан список 12 пятиугольников с обходом против часовой стрелки при взгляде снаружи.
Каждый выпуклый пятиугольник разбит веером `(0,1,2)`, `(0,2,3)`, `(0,3,4)`.
Итого **20 вершин, 12 граней, 36 треугольников, 108 индексов uint32_t**, без дублирования вершин.
Структура `Vertex` содержит `glm::vec3 position` и `glm::vec3 color`.
RGB вычисляется из локальной позиции как `(position / phi + 1) * 0.5` и интерполируется между вершинами.

Тест `dodecahedron-geometry` проверяет уникальность вершин, общую описанную сферу, планарность,
равенство сторон и внутренних углов 108°, выпуклость, 30 рёбер с двумя смежными гранями,
согласованный обход, наружные нормали треугольников и совпадение площади триангуляции с площадью граней.

## Vulkan и MVP

`graphics.cpp/.hpp` содержит только небольшой helper для создания, записи и уничтожения mapped-буферов VMA.
Приложение владеет vertex buffer, index buffer и uniform buffer, их тремя VMA allocations,
descriptor set layout, descriptor pool/set, pipeline layout и graphics pipeline.
Два временных shader module уничтожаются сразу после создания pipeline, включая путь ошибки.
Остальные ресурсы уничтожаются в `application::shutdown()` после ожидания graphics queue,
до уничтожения VMA allocator. Частично созданные ресурсы также очищаются при ошибке initialize.

UBO содержит 80-байтовую `alignas(16)` структуру: `glm::mat4` по смещению 0 и
`glm::vec4 user_color` по смещению 64. Размер и смещения проверяются `static_assert`.
Descriptor set 0, binding 0 имеет тип `UNIFORM_BUFFER` и доступен vertex shader;
GLSL объявляет ту же матрицу через `std140`. Матрица передаётся как `projection * view * model`.
Model строится из ручных Position/Rotation/Scale и добавочного движения/вращения анимации.
Исходные повороты — 5° вокруг X и 10° вокруг Y; анимация изначально на паузе.
Камера `(3.5,3.5,3.5)` смотрит в начало координат. Исходный FOV 45°, near=0.1, far=100.
GLM `perspectiveRH_ZO` и `orthoRH_ZO` используют глубину [0,1] и инверсию Y для Vulkan viewport.
Aspect ratio вычисляется из текущего `swapchain_extent` после обработки resize.

В `main.cpp` существующий `prepare()` перенесён перед `update()`: ожидание fence предыдущего кадра
завершается до перезаписи общего UBO. `vmaFlushAllocation` обеспечивает видимость записи и для
памяти без HOST_COHERENT. Starter использует один кадр в полёте, поэтому одного UBO достаточно.

Pipeline использует triangle list, back-face culling, `COUNTER_CLOCKWISE`, depth test/write `LESS`
и существующий render pass/depth buffer. Viewport и scissor динамические.
В `render()` сохранены reset/begin/end command buffer и начало/конец существующего render pass;
добавлены bind pipeline, vertex/index buffers, descriptor set и `vkCmdDrawIndexed(108, ...)`.
Окно `Lab 1 Controls` использует отдельный существующий проход ImGui.
`graphics_internal.cpp` и публичный интерфейс application не изменены.

## Сборка и запуск в настроенном Windows-окружении

Из текущей рабочей папки PowerShell:

```powershell
& .\.tools\build.ps1
. .\.tools\environment.ps1
ctest --test-dir build-debug --output-on-failure
& .\.tools\start.ps1
```

Build helper активирует установленный MinGW, CMake и Vulkan SDK 1.4.363.0, использует ASCII junction
рабочей папки и выполняет команды автора:

```powershell
cmake --preset mingw-debug
cmake --build build-debug --parallel 4
```

Приложение запускается из корня проекта; шейдеры загружаются по относительным путям `shaders/*.spv`.
CMake скачивает закреплённую GLM 1.0.3 и компилирует GLSL через существующий механизм `glslc`.
`glslc` обязателен при конфигурации, поэтому сборка не может незаметно пропустить шейдеры.

Автоматическая проверка с Khronos Validation и Synchronization Validation:

```powershell
& .\.tools\verify.ps1 -RenderSeconds 8 -ExerciseWindow -RequireClean
```

Логи сборки: `.tools/logs/configure.log`, `.tools/logs/build.log`.
Логи последнего запуска: `.tools/logs/starter-stdout.log`, `.tools/logs/starter-stderr.log`;
результаты проверки: `.tools/logs/verification.json`; screenshot: `.tools/logs/starter-window.png`.
Артефакты стабильной базы сохранены отдельно в `.tools/logs/stable-19a2084/`.

## Историческая проверка базовой версии

Проверено 2 октября 2026 года на Windows, Intel UHD Graphics, Vulkan SDK **1.4.363.0**:

- Debug-сборка MinGW завершилась успешно; оба GLSL скомпилированы `glslc`.
- `ctest`: **1/1 PASS**; оба SPIR-V модуля прошли `spirv-val --target-env vulkan1.0`.
- Khronos Validation Layer действительно загружен; включены Core Checks и Synchronization Validation.
- После 8 секунд отрисовки проверены resize до 1000×650, затем 1280×720, minimize/restore.
- Итог: **0 Validation ERROR, 0 Validation WARNING, exit code 0** после штатного закрытия.
- Screenshot просмотрен: объект отображается объёмно, ImGui находится справа, не закрывая его.

В диагностическом stderr во время resize встречается обычное сообщение starter
`Swapchain is suboptimal for rendering!` о результате `VK_SUBOPTIMAL_KHR`;
оно обрабатывается пересозданием swapchain и не является сообщением Validation Layers.

Созданы: `source/dodecahedron.cpp/.hpp`, `source/graphics.cpp/.hpp`,
`shaders/dodecahedron.vert`, `shaders/dodecahedron.frag`, `tests/dodecahedron_tests.cpp`, `LAB1.md`.
Изменены: `source/application.cpp`, `source/main.cpp`, `CMakeLists.txt`.
Эта базовая реализация теперь сохранена коммитом `36cbd01`.
Результаты текущей версии с заданиями 1–5 приведены в [LAB1_EXTRAS.md](LAB1_EXTRAS.md).

Документация зависимостей: [GLM](https://github.com/g-truc/glm/releases/tag/1.0.3),
[VMA mapped memory](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html),
[Vulkan front face](https://docs.vulkan.org/refpages/latest/refpages/source/VkFrontFace.html).
