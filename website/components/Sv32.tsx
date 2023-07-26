"use client";

import { useState } from "react";

function isValidVaddr(vaddr: number) {
  if (vaddr !== Math.floor(vaddr)) return false;
  if (vaddr !== vaddr) return false; // is Nan?
  if (vaddr < 0) return false;
  if (vaddr > 0xffffffff) return false;
  return true;
}

export default function Sv32() {
  const [input, setInput] = useState("80001234");
  let vaddr: number;
  if (/^[0-9a-fA-F]+$/.test(input)) {
    vaddr = parseInt(input, 16);
  } else {
    vaddr = NaN;
  }
  const firstLevelIndex = (vaddr >> 22) & 0x3ff;
  const secondLevelIndex = (vaddr >> 12) & 0x3ff;
  const offset = vaddr & 0xfff;

  const addressChanged = (e: any) => {
    setInput(e.target.value);
  };

  return (
    <div className="flex justify-center items-center">
      <div>
        <input
          value={input}
          onChange={addressChanged}
          className="block mb-2 text-2xl font-mono text-center text-gray-900 border-gray-800 border w-full"
        />
        <div>
          {isValidVaddr(vaddr)
            ? vaddr
                .toString(2)
                .padStart(32, "0")
                .split("")
                .map((bit, i) => {
                  const index = 31 - i;
                  let color = "";
                  if (index >= 22) color = "bg-red-400";
                  else if (index >= 12) color = "bg-blue-400";
                  else color = "bg-green-400";
                  return (
                    <span
                      className={`font-mono w-4 inline-block text-gray-900 text-center ${color}`}
                      key={index}
                    >
                      {bit}
                    </span>
                  );
                })
            : "不正なアドレスです"}
          <br />
          <span className="w-40 mt-2 text-sm font-mono inline-block text-center">
            VPN[1]
            <br />
            <span className="text-sm">(10ビット)</span>
          </span>
          <span className="w-40 mt-2 text-sm font-mono inline-block text-center">
            VPN[0]
            <br />
            <span className="text-sm">(10ビット)</span>
          </span>
          <span className="w-48 mt-2 text-sm font-mono inline-block text-center">
            page offset
            <br />
            <span className="text-sm">(12ビット)</span>
          </span>
        </div>
        <div className="mt-2 flex justify-center">
          <table className="mt-2">
            <tbody>
              <tr>
                <td className="font-bold pr-2">1段目のエントリインデックス:</td>
                <td>
                  <span className="font-mono w-16 inline-block text-center text-gray-900 bg-red-400">
                    {isValidVaddr(vaddr) ? firstLevelIndex : ""}
                  </span>
                  <span className="pl-2 text-sm">(10進数)</span>
                </td>
              </tr>
              <tr>
                <td className="font-bold pr-2">2段目のエントリインデックス:</td>
                <td>
                  <span className="font-mono w-16 inline-block text-center text-gray-900 bg-blue-400">
                    {isValidVaddr(vaddr) ? secondLevelIndex : ""}
                  </span>
                  <span className="pl-2 text-sm">(10進数)</span>
                </td>
              </tr>
              <tr>
                <td className="font-bold pr-2">ページ内オフセット:</td>
                <td>
                  <span className="font-mono w-16 inline-block text-center text-gray-900 bg-green-400">
                    {isValidVaddr(vaddr) ? offset.toString(16) : ""}
                  </span>
                  <span className="pl-2 text-sm">(16進数)</span>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
}
