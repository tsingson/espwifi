package main

import (
	"fmt"
	"io/ioutil"
	"net/http"
)

func main() {
	// 确保将 IP 替换为你 ESP32 在路由器中实际获取到的地址
	resp, err := http.Get("http://192.168.1.100/get_pin")
	if err != nil {
		fmt.Println("请求失败:", err)
		return
	}
	defer resp.Body.Close()

	body, _ := ioutil.ReadAll(resp.Body)
	fmt.Printf("D2 引脚当前状态: %s\n", string(body))
}