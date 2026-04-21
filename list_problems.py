import requests
import os

token = os.environ.get("ACMOJ_TOKEN")
headers = {"Authorization": f"Bearer {token}"}
url = "https://acm.sjtu.edu.cn/OnlineJudge/api/v1/problems"
response = requests.get(url, headers=headers)
print(response.status_code)
# Only print IDs and titles to save space
if response.status_code == 200:
    data = response.json()
    for p in data.get('problems', []):
        print(f"{p['id']}: {p['title']}")
