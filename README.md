## Building

```console
$ ./go.sh docker build
$ ./go.sh docker start
$ ./go.sh docker exec ./go.sh cmake configure
$ ./go.sh docker exec ./go.sh cmake build
$ ./go.sh docker exec python3 -m virtualenv ./venv
$ ./go.sh docker exec ./venv/bin/python3 -m pip install flask
$ ./go.sh docker exec ./venv/bin/python3 server.py --renderer-executable ./build/osprayAsAService
$
$ # Open http://localhost:8081/
```
