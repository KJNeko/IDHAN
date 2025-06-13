# Config file location

- IDHAN will look for a config file next to it's executable first (`./config.toml`)
- If one cannot be found there it will look for one in `~/.config/idhan/config.toml`, You can also pass the config to executable (see launch options)
- IDHAN will only create a config file if it can't find one

# Launch options

- `-h` `--help`: Self explanitory
- `--testmode`: Forces postgresql db schema to use the schema `test` instead of `public` (Used for running automated tests)
- `--use_stdout`: Enables the logger to output to stdout (Useful for preventing test output clutter)
- `--config <PATH>` Overrides the config location. IDHAN will not load configs from other locations.
- `--pg_user` Specifies a postgresql users to use (Overrides the config file)
- `--pg_host` Specifies a hostname for the PG server to be found at (Overrides the config file)

# Config options (`config.toml`)

The following is a example config file to use

```
[database]
#host = "localhost"
#port = 5432
#user = "idhan"
#password = "your_password_here"
#database = "idhan-db"


[server]
#host = "0.0.0.0"
#port = 16609
#threads = 4
#use_stdout = true

[logging]
# Loggin level: trace, debug, info, warn, error, critical
level = "info"
# Logging output location
#file = "./logs/idhan.log"
```





