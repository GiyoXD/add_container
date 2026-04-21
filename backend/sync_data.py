import sqlite3
import os
from googleapiclient.discovery import build
from google.oauth2 import service_account
from googleapiclient.errors import HttpError

# --- CONFIGURATION ---
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
SERVICE_ACCOUNT_FILE = os.path.join(BASE_DIR, 'secret.json')
DB_FILE = os.path.join(BASE_DIR, 'shipping_data.db')
SCOPES = ['https://www.googleapis.com/auth/spreadsheets']
# The ID from your specific shipping sheet
SPREADSHEET_ID = '1piQv1mpEWWBw3hUITAD2Q5ZHdvnqP38n0OLUbK5Y1Hk'
# Range: Adjust "Sheet1" to your tab name. T50000 covers your large dataset.
DATA_RANGE = "CONTAINER!A2:J5000" 

def setup_database():
    """Creates the SQLite table if it doesn't exist."""
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS container_table (
            bill TEXT,
            invoice_no TEXT,
            container_no TEXT,
            type TEXT,
            seal_no TEXT,
            truck_no TEXT,
            driver_name TEXT,
            cnee TEXT,
            date TEXT,
            PRIMARY KEY (bill, container_no, invoice_no)
        )
    ''')
    conn.commit()
    return conn

def sync_sheets_to_sqlite():
    """Pulls data from Google Sheets and syncs it to local SQLite."""
    # 1. Authenticate with Google
    if not os.path.exists(SERVICE_ACCOUNT_FILE):
        print(f"Error: {SERVICE_ACCOUNT_FILE} not found!")
        return

    creds = service_account.Credentials.from_service_account_file(
            SERVICE_ACCOUNT_FILE, scopes=SCOPES)
    service = build('sheets', 'v4', credentials=creds)

    try:
        # 2. Pull data (This is 1 API Read Call)
        print("Fetching data from Google Sheets...")
        result = service.spreadsheets().values().get(
            spreadsheetId=SPREADSHEET_ID, 
            range=DATA_RANGE
        ).execute()
        
        rows = result.get('values', [])
        if not rows:
            print("No data found in the sheet.")
            return

        # 3. Handle Edge Cases (Cleaning Data)
        clean_data = []
        for row in rows:
            # Skip completely empty rows
            if not any(row):
                continue
            
            # Ensure row has at least 9 columns
            while len(row) < 9:
                row.append("")
            
            # Take only the first 9 columns
            clean_data.append(tuple(row[:9]))

        # 4. Save to SQLite
        conn = setup_database()
        cursor = conn.cursor()
        
        # INSERT OR IGNORE avoids errors with duplicate BILL/INVOICE numbers
        cursor.executemany('''
            INSERT OR IGNORE INTO container_table 
            (bill, invoice_no, container_no, type, seal_no, truck_no, driver_name, cnee, date)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        ''', clean_data)
        
        conn.commit()
        print(f"Successfully synced {len(clean_data)} rows to local database.")
        conn.close()

    except HttpError as err:
        # Handles 403 (Permission) or 429 (Rate Limit) errors
        print(f"Google API Error: {err}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")

if __name__ == "__main__":
    sync_sheets_to_sqlite()
