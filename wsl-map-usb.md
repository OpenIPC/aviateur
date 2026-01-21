Refer to https://learn.microsoft.com/en-us/windows/wsl/connect-usb first.

```powershell
usbipd list
```

```powershell
usbipd bind --busid 3-4
```

```powershell
usbipd attach --wsl --busid 3-4
```
