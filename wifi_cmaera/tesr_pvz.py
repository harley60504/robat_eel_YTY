import requests

url = "https://mp-tw-openapi.auto.mydlink.com/v2/device/control"
token = "oYSx5D6lfy4Rx3PzUrnjLwXE_dPno61k"

params = {
    "access_token": token
}

payload = {
    "data": {
        "mydlink_id": "55819424",
        "uid": 0,
        "idx": 0,
        "ctrl_id": 26,
        "value": {
            "p": 250,   # 左右角度（0~340 左右）
            "t": 80,    # 上下角度（0~90）
            "z": 0
        }
    }
}

r = requests.post(url, params=params, json=payload, timeout=5)
print(r.status_code)
print(r.text)
