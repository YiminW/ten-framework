# 独立测试用例修改说明

## 问题描述

原来的测试文件 `test_invalid_text_handling.py` 使用单个测试对象运行 27 个测试用例，这导致以下问题：

1. **状态污染**: 如果中间某个测试用例失败，被测试对象的状态可能异常，影响后续测试用例
2. **资源泄漏**: 前一个测试用例可能没有正确清理资源
3. **连接问题**: WebSocket 连接可能因为前一个测试用例的问题而异常
4. **测试隔离性差**: 测试用例之间相互影响，降低了测试的可靠性

## 解决方案

### 1. 创建独立的测试器类

新增 `SingleTestCaseTester` 类，专门用于运行单个测试用例：

```python
class SingleTestCaseTester(AsyncExtensionTester):
    """单个测试用例的测试器，每个测试用例独立运行"""
    
    def __init__(self, test_index: int, invalid_text: str, valid_text: str, session_id: str):
        super().__init__()
        self.test_index = test_index
        self.invalid_text = invalid_text
        self.valid_text = valid_text
        self.session_id = session_id
        
        # 测试状态
        self.received_audio_frame: bool = False
        self.received_tts_output: bool = False
        self.received_error: bool = False
        self.test_success: bool = False
```

### 2. 修改主测试函数

将测试用例定义移到主函数中，为每个测试用例创建独立的测试器：

```python
def test_invalid_text_handling(extension_name: str, config_dir: str) -> None:
    # 定义测试用例
    test_cases = [
        {"invalid": "", "valid": "Hello world."},
        {"invalid": " ", "valid": "This is a test."},
        # ... 更多测试用例
    ]
    
    # 为每个测试用例创建独立的测试器
    for i, test_case in enumerate(test_cases):
        tester = SingleTestCaseTester(
            test_index=i,
            invalid_text=test_case["invalid"],
            valid_text=test_case["valid"],
            session_id=f"test_invalid_text_session_{i}"
        )
        
        # 设置测试模式并运行
        tester.set_test_mode_single(extension_name, json.dumps(config))
        error = tester.run()
        
        # 记录测试结果
        test_result = {
            "test_index": i,
            "invalid_text": test_case["invalid"],
            "valid_text": test_case["valid"],
            "success": tester.test_success,
            "error": error
        }
        all_test_results.append(test_result)
```

### 3. 独立的测试流程

每个 `SingleTestCaseTester` 实例：

1. **独立初始化**: 每个测试用例都有独立的 session_id 和 request_id
2. **独立运行**: 每个测试用例都启动一个全新的扩展实例
3. **独立清理**: 每个测试用例完成后自动清理资源
4. **独立结果**: 每个测试用例的结果独立记录

## 修改的文件

### 1. test_invalid_text_handling.py

**新增内容**:
- `SingleTestCaseTester` 类
- 独立的测试用例运行逻辑
- 独立的错误验证方法

**删除内容**:
- 原来的 `_run_single_test` 方法
- 原来的 `_reset_test_state` 方法
- 原来的 `_send_tts_text_input` 方法
- 原来的 `_validate_error_response` 方法
- 原来的 `on_data` 和 `on_audio_frame` 方法（在 `InvalidTextHandlingTester` 中）

**保留内容**:
- `InvalidTextHandlingTester` 类（作为主测试器）
- 测试用例定义（移到主函数中）

## 优势

### 1. 测试隔离性
- 每个测试用例都有独立的扩展实例
- 测试用例之间不会相互影响
- 状态污染问题得到解决

### 2. 错误定位
- 每个测试用例的失败都能准确定位
- 错误信息更加清晰
- 便于调试和修复

### 3. 资源管理
- 每个测试用例都有独立的资源管理
- 避免资源泄漏
- 更好的内存使用

### 4. 并发安全
- 测试用例可以并行运行（如果需要）
- 避免并发冲突
- 提高测试效率

## 使用方式

```python
# 运行所有测试用例
test_invalid_text_handling("elevenlabs_tts_python", "./config")
```

## 测试结果

测试完成后会输出详细的测试结果摘要：

```
📊 TEST RESULTS SUMMARY
==========================================
Total test cases: 27
Passed: 25
Failed: 2

❌ Some tests failed!
  - Test 15 failed
    Invalid text: '('
    Valid text: 'English punctuation test.'
  - Test 23 failed
    Invalid text: 'H₂O'
    Valid text: 'Chemical formula test.'
```

## 注意事项

1. **测试时间**: 由于每个测试用例都独立启动扩展实例，总测试时间会有所增加
2. **资源消耗**: 每个测试用例都会消耗一定的系统资源
3. **日志输出**: 每个测试用例都有独立的日志输出，便于调试
4. **错误处理**: 每个测试用例的错误都会被独立捕获和记录 