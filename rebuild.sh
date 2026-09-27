rm -rf build sdkconfig
idf.py set-target esp32c6
idf.py build
idf.py flash monitor

