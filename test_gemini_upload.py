import requests
import json
import os

API_KEY = "YOUR_GEMINI_API_KEY"
# Use any small image file for testing
TEST_IMAGE = "backend/test_image.png" # I'll check if any image exists or create a dummy

def test_upload(image_path):
    url = f"https://generativelanguage.googleapis.com/upload/v1beta/files?key={API_KEY}"
    
    metadata = {"file": {"display_name": os.path.basename(image_path)}}
    
    files = {
        'metadata': (None, json.dumps(metadata), 'application/json'),
        'file': (os.path.basename(image_path), open(image_path, 'rb'), 'image/png')
    }
    
    # Gemini requires X-Goog-Upload-Protocol: multipart for this endpoint
    headers = {
        "X-Goog-Upload-Protocol": "multipart"
    }
    
    print(f"Uploading {image_path}...")
    response = requests.post(url, headers=headers, files=files)
    
    print(f"Status: {response.status_code}")
    print(f"Response: {response.text}")

if __name__ == "__main__":
    # Create a dummy image if it doesn't exist for the test
    if not os.path.exists(TEST_IMAGE):
        with open(TEST_IMAGE, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\nIDATx\x9cc\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4\x00\x00\x00\x00IEND\xaeB`\x82")
    
    test_upload(TEST_IMAGE)
