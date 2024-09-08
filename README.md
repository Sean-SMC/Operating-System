# Simulated Operating System Functions

This repository contains a series of projects focused on implementing core functions from a simulated operating system. Each project builds upon the previous one, refining the system by removing unnecessary features while introducing new concepts and complexities. These projects are organized into six distinct folders, each representing a crucial component of operating system functionality.

## Project Structure

1. **Forking Processes**  
   This project introduces process creation and management through the simulation of process forking.

2. **System Clock & Process Table**  
   In this project, the focus is on managing processes with a system clock and process table. It builds on process creation by adding timing and tracking of active processes.

3. **Shared Memory & Message Queues**  
   This project adds inter-process communication (IPC) through shared memory and message queues, facilitating data exchange between processes.

4. **Process Scheduling: Multi-Level Feedback Queue**  
   Here, process scheduling is introduced using the Multi-Level Feedback Queue (MLFQ) algorithm. Processes are managed across different priority levels to optimize CPU usage.

5. **Resource Management: Banker's Algorithm**  
   This project implements the Banker's Algorithm to manage resources and avoid deadlock situations in the system.

6. **Memory Management: Page and Frame Tables**  
   The final project focuses on memory management, specifically implementing page and frame tables to simulate virtual memory and paging.

## How to Navigate This Repository

Each folder represents a standalone project and includes its own README file. These READMEs provide detailed information about the specific tasks being implemented, the purpose of the project, and instructions for executing the code.

- **Step 1:** Start with the "Forking Processes" project to get an understanding of the basic process management in this simulated OS.
- **Step 2:** Progress through the folders sequentially, as each project builds on the concepts and functionality of the previous one.
  
## Getting Started

To run any of the projects:

1. Navigate to the corresponding folder.
2. Follow the instructions in the README file to set up and execute the project.

Each project can be run independently, allowing you to focus on the specific functionality being implemented without needing to configure the entire system.
