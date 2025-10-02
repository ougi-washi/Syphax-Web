# Syphax-Web
Web in C.

### Features
* Web server
* HTML builder (with translation support)

### Building
CMake 3.22.1 is required.

Simply run:
```bash
build.sh
```
or
```bash
build.bat
```

### Usage
Run
```bash
./index
```
then access the default ```http://127.0.0.1:8000/```

### CMake module
```cmake
target_link_libraries(my_target PUBLIC syphax-web::main)
```

### License
MIT License

