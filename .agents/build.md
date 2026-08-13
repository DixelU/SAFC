# Local build command

Use the non-`amd64` MSBuild launcher on this machine:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\Progs\GitHub\SAFC\_SAFC_.sln' `
  /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
```

Codex desktop may expose duplicate `PATH` and `Path` environment entries. If
MSBuild reports `MSB6001` with "Key in dictionary: PATH / Adding key: Path",
normalize the child `cmd.exe` environment before invoking the command; do not
change the user's system environment.
