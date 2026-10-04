# Лабораторная №1, вариант 10: дополнительные задания 1–5

Ветка `lab1-v10`. Базовая лабораторная отдельно сохранена коммитом **36cbd01**;
исправления starter находятся в **19a2084**. Геометрия сохраняет 20 уникальных вершин,
12 пятиугольников, 36 треугольников и 108 индексов. Дополнительное задание №6 не выполнено.

## 1. Проекция

В одном окне `Lab 1 Controls` доступны Perspective и Orthographic.
Используются настоящие GLM `perspectiveRH_ZO` и `orthoRH_ZO`, near=0.1, far=100,
глубина [0,1] и инверсия Y для положительной высоты Vulkan viewport.
Aspect ratio вычисляется из актуального swapchain extent после `prepare()`.
Perspective позволяет менять FOV от 15° до 90°; исходное значение 45°.
Orthographic позволяет менять Half height от 1 до 8; исходное значение 2.5.
Ортографические границы: X=±Half height×aspect, Y=±Half height.

## 2. Transform

Position, Rotation и Scale имеют по три поля X/Y/Z. Вращение задаётся в градусах.
Матрица модели: `T(base position + animation offset) * Rx * Ry * Rz * S`;
к ручным углам добавляются углы анимации. Scale ограничен диапазоном 0.05–5:
ограничение действует и при перетаскивании, и при вводе с клавиатуры, дополнительно проверяется в состоянии.
Reset Transform возвращает Position=(0,0,0), Rotation=(5°,10°,0°), Scale=(1,1,1).
Эта кнопка сбрасывает ручной transform; для анимации предусмотрена отдельная Reset Animation.

## 3. Анимация

Play/Pause, Reset Animation, Speed, Radius, Vertical amplitude.
Начальное состояние — Pause, Speed=1, Radius=0.75, Vertical amplitude=0.4.
Движение задаётся пространственной траекторией:

```text
x = radius * sin(phase)
y = verticalAmplitude * sin(2 * phase)
z = radius * (cos(phase) - 1)
```

Сдвиг косинуса позволяет начать в исходной позиции. Дополнительно объект вращается
на `(0.45, 0.7, 0.3) * phase` радиан вокруг трёх осей.
Фаза интегрируется как `phase += deltaTime * speed` только при Play.
При Pause обновляется отсчёт времени, но фаза сохраняется: возобновление не включает время паузы.
Изменение Speed действует на следующие шаги времени и не пересчитывает накопленную фазу.
Reset Animation возвращает фазу в 0; ручные параметры сохраняются.

## 4. Цвет через ImGui

ColorEdit3 задаёт RGB-множитель. Белый (1,1,1) сохраняет исходный процедурный цвет.
Множитель передаётся через существующий UBO и descriptor set без пересоздания pipeline.
Vertex shader вычисляет `vertexColor * userColor.rgb`; fragment shader выводит интерполированный результат.
Умножение на одинаковый для всех вершин множитель сохраняет интерполяцию цвета.

UBO `TransformUniform`: 80 байт, выравнивание 16; MVP по смещению 0, vec4 user_color по смещению 64.
C++ проверяет эти значения `static_assert`. Проверенный SPIR-V содержит
`MatrixStride 16`, `Offset 0` и `Offset 64`, совпадающие с C++.
Descriptor range равен размеру всей структуры. Запись выполняется после ожидания fence предыдущего кадра.

## 5. Процедурный цвет вершин

RGB вычисляется из локальной позиции: `color = (position / phi + 1) * 0.5`.
Это отображение координат [-phi,phi] в RGB [0,1]; ручная палитра удалена.
В vertex buffer остаются position и color; все 20 вершин имеют различные цвета.
Геометрия загружается один раз; обновляется только uniform buffer.

## Архитектура и файлы

Состояние находится в `ControlsState`, `Transform`, `Animation` в `source/application_state.hpp/.cpp`.
Математика отделена от ImGui/Vulkan, чтобы проверить преобразования и время независимо от GPU.
Существующие vertex/index buffers, descriptors, pipeline и render pass используются дальше.
`graphics_internal.cpp`, `graphics.cpp/.hpp`, `main.cpp`, `application.hpp` и fragment shader не изменены.
Все Vulkan-ресурсы по-прежнему освобождаются при shutdown после завершения работы GPU.

Изменены: `CMakeLists.txt`, `source/application.cpp`, `source/dodecahedron.cpp`,
`shaders/dodecahedron.vert`, `tests/dodecahedron_tests.cpp`, `LAB1.md`.
Созданы: `source/application_state.hpp/.cpp`, `tests/application_state_tests.cpp`,
`tests/ui_observer.hpp/.cpp`, `tests/verify_controls.ps1`, `tests/verify_images.py`, `LAB1_EXTRAS.md`.

## Автоматические проверки

Проверено 4 октября 2026 года, Windows, Intel UHD Graphics, Vulkan SDK 1.4.363.0.

- CTest: геометрия/процедурные цвета и матрицы/анимация; **2/2 PASS**.
- Проверены Vulkan depth [0,1], aspect ratio обеих проекций, вращение по трём осям,
  положительный scale, Reset Transform, Pause/Play без скачка, Speed/Radius/Reset Animation,
  одинаковое движение при 30 и 240 FPS.
- Desktop-тест использует реальные клики и клавиатурный ввод в ImGui: **41 PASS**.
  Он переключает Perspective→Orthographic→Perspective, меняет каждую ось transform,
  проверяет отрицательный ввод Scale, FOV, Half height, Play/Pause, скорость, радиус,
  амплитуду, цвет, resize в обеих проекциях, minimize/restore и штатное закрытие.
- Измеренная скорость фазы: 1 при Speed=1 и 2 при Speed=2.
- Проверка screenshot подтверждает изменение реального изображения при Scale и ColorEdit;
  0.6 по трём осям уменьшает площадь изображения примерно до 34%.
  На поверхности обнаружены более 25 тысяч различных RGB, подтверждающих интерполяцию.
- Core и Synchronization Validation включены: **0 ERROR, 0 WARNING, exit code 0**.

`LAB1_ENABLE_UI_TEST_HOOKS` по умолчанию OFF. В тестовой сборке наблюдатель только читает
состояние и координаты виджетов после настоящих действий, не изменяет состояние и команды Vulkan.
По окончании проверки проект пересобирается с этим параметром OFF; Validation Layers остаются включены.

Сборка и обычный запуск в настроенном окружении:

```powershell
& .\.tools\build.ps1
& .\.tools\start.ps1
```

Повторение автоматического UI-теста (требуется обычный рабочий стол Windows):

```powershell
. .\.tools\environment.ps1
cmake --preset mingw-debug -DLAB1_ENABLE_UI_TEST_HOOKS=ON
cmake --build build-debug --parallel 4
ctest --test-dir build-debug --output-on-failure
& .\tests\verify_controls.ps1
python .\tests\verify_images.py
cmake --preset mingw-debug -DLAB1_ENABLE_UI_TEST_HOOKS=OFF
cmake --build build-debug --parallel 4
& .\.tools\verify.ps1 -RenderSeconds 8 -ExerciseWindow -RequireClean
```

Python-проверка изображения использует уже имеющиеся NumPy/Pillow и не нужна для сборки приложения.
Отчёты UI-теста: `.tools/logs/extras/result.json`, `checks.json`, `snapshots.json`, `image-checks.json`.
Логи Validation Layers этого теста: `.tools/logs/extras/stdout.log`, `stderr.log`.
Screenshots: `.tools/logs/extras/perspective.png`, `orthographic.png`, `tinted.png`, `scaled.png`.
Обычный финальный запуск: `.tools/logs/verification.json`, `starter-stdout.log`, `starter-stderr.log`, `starter-window.png`.
