// Verifies the dashboard can reach MongoDB and shows what data is stored.
// Usage: node test-connection.js  (requires a filled-in .env next to this file)
require('dotenv').config();
const { MongoClient } = require('mongodb');

const { MONGODB_URL, DB_NAME, COLLECTION_NAME } = process.env;

async function main() {
  if (!MONGODB_URL || !DB_NAME || !COLLECTION_NAME) {
    console.error('Missing MONGODB_URL, DB_NAME or COLLECTION_NAME in .env');
    process.exit(1);
  }

  const client = new MongoClient(MONGODB_URL);
  try {
    await client.connect();
    console.log('✓ Connected to MongoDB');

    const collection = client.db(DB_NAME).collection(COLLECTION_NAME);
    const count = await collection.countDocuments();
    console.log(`✓ Database "${DB_NAME}", collection "${COLLECTION_NAME}": ${count} readings`);

    if (count > 0) {
      const latest = await collection.find().sort({ timestamp: -1 }).limit(1).next();
      console.log('✓ Latest reading:', JSON.stringify(latest, null, 2));
    } else {
      console.log('! No readings yet - check that the ESP32 is sending data to the gateway');
    }
  } catch (err) {
    console.error('✗ Connection failed:', err.message);
    process.exit(1);
  } finally {
    await client.close();
  }
}

main();
