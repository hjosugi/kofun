# Native CLI tutorial

Create a project:

```sh
./bin/kofun new demo-cli --template cli
cd demo-cli
../bin/kofun build src/main.kofun --framework cli -o build/demo-cli
./build/demo-cli --help
./build/demo-cli greet World --shout
```

The generated project contains a declarative application:

```kofun
cli demo_cli {
    name "demo-cli"
    version "0.1.0"
    about "A native Kofun CLI application"

    command greet {
        about "Greet a person"
        position NAME "Person to greet"
        option shout "--shout" bool "Uppercase the greeting"
        option prefix "--prefix" text "Greeting prefix" default "Hello"
        action greet
    }

    command status {
        about "Render a terminal-aware progress status"
        position LABEL "Status label"
        action status
    }
}
```

The help page and command dispatch both come from this declaration. Renaming
`command greet` changes both the generated command row and the accepted runtime
subcommand. There is no separately maintained help string.

Text options accept either `--prefix Welcome` or `--prefix=Welcome`. Boolean
options are present or absent. `--` stops option parsing, so
`greet -- --shout` greets a person literally named `--shout`.

The complete example also demonstrates the other bounded actions:

```sh
./build/kofun-tool sum -8 50
KOFUN_DEMO=runtime ./build/kofun-tool env KOFUN_DEMO
./build/kofun-tool status compiling
```

`status` prints a stable line when redirected. On a terminal it renders a
replaceable progress line and a final status. ANSI colors are used only when
stdout passes the Linux `TCGETS` terminal check and `NO_COLOR` is absent.
