import pandas as pd
import re

def extract_reason(error_text):
    if pd.isna(error_text):
        return "Unknown"
    
    error_text = str(error_text)
    
    # Check for Spotify/YouTube rate limits
    if "rate/request limit" in error_text.lower() or "rate limit" in error_text.lower():
        return "Rate limit reached"
    
    # Check for Track/Song no longer existing errors
    song_error_match = re.search(r'(SongError:.*?)(?:\n|$)', error_text)
    if song_error_match:
        return song_error_match.group(1).strip()
        
    # If it's a raw python traceback crash, grab the last readable line of the error
    lines = [line.strip() for line in error_text.split('\n') if line.strip()]
    if lines:
        return lines[-1]
        
    return "Unknown Error"

def clean_failed_csv(input_filename, output_filename):
    try:
        # Read the CSV 
        df = pd.read_csv(input_filename)
        
        # Apply the parsing function to the 'last_error' column
        df['Reason'] = df['last_error'].apply(extract_reason)
        
        # Drop the messy multiline error column
        df = df.drop(columns=['last_error'])
        
        # Save to a new CSV file
        df.to_csv(output_filename, index=False)
        print(f"Successfully processed {input_filename} -> Saved to {output_filename}")
        
    except Exception as e:
        print(f"Error processing {input_filename}: {e}")

# Run the script on your files
clean_failed_csv("failed_downloads.csv", "cleaned_failed_downloads.csv")
clean_failed_csv("failed.csv", "cleaned_failed.csv")