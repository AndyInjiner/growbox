#  Интеграция Wokwi симуляции в проект Growbox

## ✅ Созданные файлы для Wokwi

Файлы были созданы в локальной папке проекта:

```
Growbox/
├── wokwi.toml          # Конфигурация Wokwi
├── diagram.json        # Схема подключений
├── WOKWI_README.md     # Документация симуляции
└── src/
    └── main.cpp        # (нужно скопировать из GitHub)
```

## 📋 Инструкция по добавлению в GitHub репозиторий

### Шаг 1: Скопируйте main.cpp из GitHub

1. Откройте: https://raw.githubusercontent.com/Andyinjiner/Growbox/main/src/main.cpp
2. Сохраните файл в: `c:\Users\andy\Documents\Platformio\Projects\Growbox\src\main.cpp`

### Шаг 2: Загрузите файлы Wokwi в GitHub

```bash
cd c:\Users\andy\Documents\Platformio\Projects\Growbox
git add wokwi.toml diagram.json WOKWI_README.md src/main.cpp
git commit -m "Add Wokwi simulation support"
git push
```

### Шаг 3: Проверьте симуляцию

После загрузки в GitHub:
1. Откройте: https://wokwi.com/projects/github/Andyinjiner/Growbox
2. Или нажмите **"Import from GitHub"** на wokwi.com

## 🔧 Альтернативный способ (через Wokwi UI)

1. Откройте https://wokwi.com/esp32
2. Скопируйте содержимое `main.cpp` в `ino` файл на Wokwi
3. В меню выберите **"Board"** → **"ESP32 DevKit"**
4. Нажмите **"Share"** → **"Export to GitHub"**

## 📝 Примечания

### Для корректной работы в Wokwi:

1. **Библиотеки**: Wokwi автоматически подключает популярные библиотеки:
   - `TFT_eSPI` ✅
   - `DHT` ✅
   - `OneButton` ✅
   - `ArduinoJson` ✅

2. **WiFi в симуляции**: 
   - Wokwi симулирует WiFi только частично
   - Точка доступа создаётся, но подключения не будет
   - Используйте Serial Monitor для отладки

3. **TFT дисплей**:
   - В Wokwi есть встроенный симулятор TFT
   - Может потребоваться настройка `User_Setup.h` для ST7789

## 🎯 Карта пинов ESP32 в симуляции

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 DevKit v1                         │
│                                                            │
│  GPIO 2   → Реле вентиляции                                │
│  GPIO 4   → Реле насоса (out)                              │
│  GPIO 5   → TFT CS                                         │
│  GPIO 12  → MOSFET (PWM Light)                             │
│  GPIO 13  → Реле насоса (soil)                             │
│  GPIO 14  → Кнопка MODE                                    │
│  GPIO 15  → Кнопка CONFIRM                                 │
│  GPIO 16  → TFT DC                                         │
│  GPIO 17  → TFT RST                                        │
│  GPIO 18  → TFT SCK                                        │
│  GPIO 19  → TFT MISO                                       │
│  GPIO 23  → TFT MOSI                                       │
│  GPIO 25  → Реле обогрева                                  │
│  GPIO 26  → Зуммер                                         │
│  GPIO 32  → DHT22 (внутри)                                 │
│  GPIO 33  → DHT22 (снаружи)                                │
│  GPIO 34  → Датчик почвы (ADC)                             │
└─────────────────────────────────────────────────────────────┘
```

## 🔗 Ссылки

- [Wokwi ESP32 Simulator](https://wokwi.com/esp32)
- [Документация Wokwi](https://docs.wokwi.com/)
- [GitHub репозиторий проекта](https://github.com/Andyinjiner/Growbox)
