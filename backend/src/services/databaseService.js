
const { Pool } = require("pg");
const config = require("../config");

const poolOptions = config.database.connectionString
  ? { connectionString: config.database.connectionString }
  : {
      host: config.database.host,
      port: config.database.port,
      user: config.database.user,
      database: config.database.name
    };

if (config.database.password !== "") {
  poolOptions.password = config.database.password;
}

const pool = new Pool(poolOptions);

pool.on("error", function reportPoolError(error) {
  console.error("SafeBox PostgreSQL pool error:", error.message);
});

module.exports = pool;
