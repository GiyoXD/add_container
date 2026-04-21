import sqlite3
import os
import sys
import google.generativeai as genai
from googleapiclient.discovery import build
from google.oauth2 import service_account

# --- CONFIGURATION ---
GEMINI_API_KEY = "YOUR_GEMINI_API_KEY" # Get this from Google AI Studio
# Use absolute path relative to this file
BASE_DIR = getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__)))
SERVICE_ACCOUNT_FILE = os.path.join(BASE_DIR, 'secret.json')
# For the DB, let's keep it in the same folder as the EXE (user's home/current dir)
# unless you want it bundled (which would make it read-only).
# Let's use the actual directory of the EXE for the database.
EXE_DIR = os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else BASE_DIR
DB_FILE = os.path.join(EXE_DIR, 'shipping_data.db')
SPREADSHEET_ID = '1piQv1mpEWWBw3hUITAD2Q5ZHdvnqP38n0OLUbK5Y1Hk'
DATA_TAB = "CONTAINER" # Change to match your exact tab name

# Setup Gemini
genai.configure(api_key=GEMINI_API_KEY)
model = genai.GenerativeModel('gemini-3.1-flash-lite-preview') 

def get_db_connection():
    # Ensure directory exists if it's somewhere else
    os.makedirs(os.path.dirname(DB_FILE), exist_ok=True)
    return sqlite3.connect(DB_FILE) 

def setup_database(conn):
    """Creates the SQLite table with all 9 columns."""
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

def exists_locally(cursor, bill, container, invoice):
    """Check if the bill/container/invoice combo already exists in SQLite."""
    cursor.execute("SELECT 1 FROM container_table WHERE bill=? AND container_no=? AND invoice_no=?", (bill, container, invoice))
    result = cursor.fetchone()
    return result is not None

def push_to_sheets_batch(data_rows):
    """Pushes multiple rows to Google Sheets in a single call."""
    if not data_rows:
        return
    if not os.path.exists(SERVICE_ACCOUNT_FILE):
        raise FileNotFoundError(f"Service account file not found: {SERVICE_ACCOUNT_FILE}")
        
    creds = service_account.Credentials.from_service_account_file(
            SERVICE_ACCOUNT_FILE, scopes=['https://www.googleapis.com/auth/spreadsheets'])
    service = build('sheets', 'v4', credentials=creds)
    
    body = {'values': data_rows}
    service.spreadsheets().values().append(
        spreadsheetId=SPREADSHEET_ID,
        range=f"{DATA_TAB}!A1",
        valueInputOption="RAW",
        body=body
    ).execute()

def save_locally_batch(conn, data_rows):
    """Saves multiple rows to SQLite."""
    cursor = conn.cursor()
    cursor.executemany('''
        INSERT OR IGNORE INTO container_table 
        (bill, invoice_no, container_no, type, seal_no, truck_no, driver_name, cnee, date) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', data_rows)
    conn.commit()

def process_image_with_gemini(image_path, client_ids):
    """Uploads the image natively via Gemini API and extracts multiple rows."""
    print(f"Uploading {image_path} to Gemini...")
    uploaded_file = genai.upload_file(path=image_path)
    
    prompt = """
    Analyze this image of a shipping spreadsheet.
    Extract all data rows. For each row, extract the following 9 columns exactly in this order:
    1. TBL NO (This is the BILL)
    2. SHIPPER (The invoice ID)
    3. CONTAINER NO.
    4. TYPE
    5. SEAL NO.
    6. TRUCK NO. (If the content here is just a truck size, replace it with the actual truck plate no. Look carefully).
    7. DRIVER NAME
    8. CNEE
    9. DATE

    Return ONLY a CSV format with one row per line. Do not include headers, labels, or any other text.
    Use a comma as the separator. If a value contains a comma, omit it or replace with a space.
    """
    
    print("Asking Gemini to analyze the image...")
    response = model.generate_content([prompt, uploaded_file])
    
    # Delete the file from Gemini to keep your workspace clean
    try:
        genai.delete_file(uploaded_file.name)
    except:
        pass
    
    lines = response.text.strip().split('\n')
    extracted_rows = []
    
    for i, line in enumerate(lines):
        if not line.strip():
            continue
        data = [item.strip() for item in line.split(',')]
        if len(data) >= 9:
            # Only keep first 9 columns
            data = data[:9]
            # Replace Shipper with provided client ID if available
            if i < len(client_ids):
                data[1] = client_ids[i]
            extracted_rows.append(data)
            
    return extracted_rows

def process_and_save(image_path, client_ids, status_callback=None):
    """Main processing logic to be called from UI."""
    conn = get_db_connection()
    setup_database(conn)
    cursor = conn.cursor()
    
    def log(msg):
        if status_callback:
            status_callback(msg)
        print(msg)

    try:
        rows = process_image_with_gemini(image_path, client_ids)
        
        if not rows:
            log("Error: Gemini could not extract any data rows.")
            return

        new_rows = []
        skip_count = 0
        
        for row in rows:
            bill = row[0]
            invoice = row[1]
            container = row[2]
            
            if exists_locally(cursor, bill, container, invoice):
                log(f"Skipping: BILL '{bill}' with Container '{container}' and Invoice '{invoice}' already exists.")
                skip_count += 1
            else:
                new_rows.append(row)
        
        if new_rows:
            log(f"Pushing {len(new_rows)} new rows to Google Sheets...")
            push_to_sheets_batch(new_rows)
            save_locally_batch(conn, new_rows)
            log(f"Process complete. Successfully processed {len(new_rows)} rows, skipped {skip_count} duplicates.")
        else:
            log(f"Process complete. No new data found. All {skip_count} rows already exist.")
        
    except Exception as e:
        log(f"An error occurred: {str(e)}")
    finally:
        conn.close()

if __name__ == "__main__":
    # Simple CLI test
    conn = get_db_connection()
    setup_database(conn)
    conn.close()
    img_path = input("Enter image path: ")
    ids_str = input("Enter invoice IDs (comma separated): ")
    ids = [id.strip() for id in ids_str.split(',')]
    process_and_save(img_path, ids)
