import requests
import os

token = os.environ.get("ACMOJ_TOKEN")
headers = {
    "Authorization": f"Bearer {token}",
    "Content-Type": "application/x-www-form-urlencoded"
}
# Try problem ID 2677
url = "https://acm.sjtu.edu.cn/OnlineJudge/api/v1/problem/2677/submit"
data = {"language": "git", "code": "https://github.com/ojbench/oj-eval-claude-code-068-20260421183037.git"}
# Maybe it expects 'code' to be the git URL or something else?
# Let's try with a dummy code if it expects source code
# data = {"language": "cpp", "code": "#include <iostream>\nint main() { return 0; }"}
response = requests.post(url, headers=headers, data=data, proxies={"https": None, "http": None})
print(f"Status Code: {response.status_code}")
print(f"Response: {response.text}")
