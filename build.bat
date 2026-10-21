if not exist build md build

cd build
cmake -DCMAKE_CONFIGURATION_TYPES=Release .. -G "Visual Studio 17 2022"
cmake --build . --config Release