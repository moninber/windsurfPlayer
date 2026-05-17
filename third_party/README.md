# Third-Party Dependencies

This repository does not vendor large third-party SDKs.

Optional database support uses MariaDB Connector/C. To enable it locally:

1. Install MariaDB Connector/C for your toolchain.
2. Configure CMake with:
   - `MEDIASTUDIO_ENABLE_MYSQL=ON`
   - `MEDIASTUDIO_MYSQL_CONNECTOR_ROOT=<path-to-connector-root>`

The main player build does not require the database dependency.
