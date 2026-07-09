// server.js
// Dashboard backend: reads data from MongoDB and exposes it to the web app

require("dotenv").config();
const express = require("express");
const mongoose = require("mongoose");
const cors = require("cors");
const path = require("path");

const app = express();

// ------------- CONFIG -------------

const MONGO_URL = process.env.MONGODB_URL;
// If the database name is in the URL (like mongodb://host/dbname), use that
// Otherwise use DB_NAME from env
const urlDbName = MONGO_URL ? MONGO_URL.split("?")[0].split("/").pop() : null;
const DB_NAME =
  urlDbName && urlDbName.length > 0 && !urlDbName.includes(".")
    ? urlDbName
    : process.env.DB_NAME;
const COLLECTION_NAME = process.env.COLLECTION_NAME;
const PORT = process.env.PORT;
const REFRESH_INTERVAL_SECONDS = Number(process.env.REFRESH_INTERVAL_SECONDS);

if (!MONGO_URL) {
  console.error("MONGODB_URL missing from .env file");
  process.exit(1);
}

if (!DB_NAME) {
  console.error("DB_NAME missing from .env file");
  process.exit(1);
}

if (!COLLECTION_NAME) {
  console.error("COLLECTION_NAME missing from .env file");
  process.exit(1);
}

if (!PORT) {
  console.error("PORT missing from .env file");
  process.exit(1);
}

if (
  !REFRESH_INTERVAL_SECONDS ||
  isNaN(REFRESH_INTERVAL_SECONDS) ||
  REFRESH_INTERVAL_SECONDS <= 0
) {
  console.error("REFRESH_INTERVAL_SECONDS missing or invalid in .env file");
  process.exit(1);
}

// ------------- MIDDLEWARE -------------

app.use(cors());
app.use(express.json());

// Serve the frontend from the public folder
app.use(express.static(path.join(__dirname, "public")));

// ------------- MONGOOSE -------------

console.log("Connecting to MongoDB...");
console.log("  Database:", DB_NAME);
console.log("  Collection:", COLLECTION_NAME);

mongoose
  .connect(MONGO_URL, { dbName: DB_NAME })
  .then(async () => {
    console.log("MongoDB connected for dashboard");

    // Check if data exists
    const count = await Reading.countDocuments();
    console.log(`Found ${count} readings in database`);

    if (count > 0) {
      const latest = await Reading.findOne().sort({ timestamp: -1 });
      console.log("Latest reading timestamp:", latest.timestamp);
    }
  })
  .catch((err) => {
    console.error("MongoDB error:", err.message);
    process.exit(1);
  });

const ReadingSchema = new mongoose.Schema(
  {
    temperature: Number,
    humidity: Number,
    pressure: Number,
    co2: Number,
    timestamp: { type: Date, default: Date.now },
  },
  { versionKey: false }
);

const Reading = mongoose.model("Reading", ReadingSchema, COLLECTION_NAME);

// ------------- API ENDPOINTS -------------

// Latest reading (for "current conditions")
app.get("/api/readings/latest", async (req, res) => {
  try {
    const doc = await Reading.findOne().sort({ timestamp: -1 }).lean();
    if (!doc) return res.json({ ok: true, reading: null });
    res.json({ ok: true, reading: doc });
  } catch (err) {
    console.error("Error /latest:", err.message);
    res.status(500).json({ ok: false, error: "Server error" });
  }
});

// History - last N readings (for charts)
app.get("/api/readings/history", async (req, res) => {
  try {
    const limit = Number(req.query.limit) || 100;
    const docs = await Reading.find()
      .sort({ timestamp: -1 })
      .limit(limit)
      .lean();

    // Return them in chronological order (oldest -> newest)
    docs.reverse();

    res.json({ ok: true, readings: docs });
  } catch (err) {
    console.error("Error /history:", err.message);
    res.status(500).json({ ok: false, error: "Server error" });
  }
});

// Simple health check
app.get("/api/health", (req, res) => {
  res.json({
    ok: true,
    mongo: mongoose.connection.readyState === 1 ? "connected" : "disconnected",
  });
});

// Configuration endpoint (for frontend)
app.get("/api/config", (req, res) => {
  res.json({
    ok: true,
    refreshIntervalSeconds: REFRESH_INTERVAL_SECONDS,
  });
});

// ------------- START SERVER -------------

app.listen(PORT, "0.0.0.0", () => {
  console.log(`Dashboard server running on port ${PORT}`);
});
