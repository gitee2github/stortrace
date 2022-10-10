# -*- coding: utf-8 -*-
from crypt import methods
from os import access
from posixpath import split
from unicodedata import category
import requests
import time
import json
import numpy as np
import json
import ast
import os
import fnmatch
import sys
from flask_cors import *
from flask import Flask,render_template,request,Response,redirect,url_for
app = Flask(__name__)
CORS(app, supports_credentials=True)


@app.route('/')
def index():
    return render_template('index.html')

@app.route('/blk_trace')
def blk_trace():
    return render_template('blk_trace.html')

@app.route('/dio_simple')
def dio_simple():
    return render_template('dio_simple.html')

@app.route('/dio_info')
def dio_info():
    return render_template('dio_info.html')

@app.route("/io_per_pmc")
def io_per_pmc():
    f = open(path+"/sum_io_pmc.json")
    data = json.load(f)
    name = "io_per_pmc"
    seq = data["seq_data"]
    f.close()
    return render_template('avg_with_mark.html',graph_name = "io_per_pmc",data=" ".join(str(i) for i in seq))

@app.route("/blk_merge_rate")
def blk_merge_rate():
    f = open(path+"/merge_rate.json")
    data = json.load(f)
    name = "blk_merge_rate"
    seq = data["rate"]
    f.close()
    return render_template('avg_with_mark.html',graph_name = "blk_merge_rate",data=" ".join(str(i) for i in seq))

@app.route("/blk_heatmap/<length>")
def blk_heatmap(length):
    drive_json = []
    for f_name in os.listdir(path):
        if(fnmatch.fnmatch(f_name,"*_heatmap.json")):
            print(f_name)
            drive_json.append(f_name.rstrip("_heatmap.json"))
    print(drive_json)
    return render_template('drive_heatmap_list.html',drives = drive_json,length=length)
    
@app.route("/query_avg/<stage>",methods=['GET'])
def query_avg(stage):
    f = open(path+"/"+stage+'_avg.json')
    data = json.load(f)
    name = data["name"]
    avgs = data["avgs"]
    f.close()
    return render_template('avg_with_mark.html',graph_name = stage+'_avg',data=" ".join(str(i) for i in avgs))

@app.route("/query_dis/<stage>",methods=['GET'])
def query_dis(stage):
    f = open(path+"/"+stage+'_dis.json')
    data = json.load(f)
    name = data["name"]
    dis = data["distribution"]
    f.close()
    return render_template('dis_with_mark.html',title = data["name"], mode = stage+"_distribution",bucket = " ".join(str(i) for i in dis))


@app.route('/dio_throughput')
def dio_throughput():
    f = open(path+'/dio_throughput.json')
    data = json.load(f)
    f.close()
    return render_template('pmc_aq.html',graph_name = data["name"],data = data["seq_data"],inv = 100)

@app.route('/dio_time_stramp')
def dio_time_stramp():
    f = open(path+'/dio_record_time.json')
    data = json.load(f)
    x_data = data["x_data"]
    y_data = data["y_data"]
    x_str = ','.join(str(x) for x in x_data)
    y_str = ','.join(str(y) for y in y_data)
    f.close()
    return render_template('record_time.html',title = data["name"], mode = "ext4_simple_dio",x_data = x_str,y_data = y_str)

@app.route('/dio_time_stramp_bucket/<acc>',methods=['GET'])
def dio_time_stramp_bucket(acc):
    f = open(path+'/dio_record_time.json')
    data = json.load(f)
    x_data = data["x_data"]
    sec_acc = float(acc)*1e9
    last_tp = int((x_data[-1])/sec_acc)+10
    bucket = [0 for i in range(last_tp)]
    for tp in x_data:
        index = int((tp)/sec_acc)
        bucket[index] += 1
    f.close()
    bucket_str =  ','.join(str(x) for x in bucket)
    return render_template('record_bucket.html',title = data["name"], mode = "ext4_info_dio",bucket = bucket_str)


@app.route('/ext4_simple_dio_latency/<split_page>')
def ext4_simple_dio_latency(split_page):
    type = ["kernel_crossing","file_system","block_io","common","file"]
    f = open(path+"/dio_event_latency.json")
    data = json.load(f)
    type_data = [data[key] for key in type]
    type_data_str = []
    for data in type_data:
        type_data_str.append(','.join(str(x) for x in data))
    all_str = ' '.join(type_data_str)
    return render_template('latency.html',data = all_str,type = ','.join(type),split_count = split_page)

@app.route('/ext4_info_dio_latency')
def ext4_info_dio_latency():
    type = ["kernel_crossing","file_system","block_io","common","file"]
    f = open(path+"/ext4_dio_info_event_latency.json")
    data = json.load(f)
    type_data = [data[key] for key in type]
    type_data_str = []
    for data in type_data:
        type_data_str.append(','.join(str(x) for x in data))
    all_str = ' '.join(type_data_str)
    return render_template('latency_info.html',data = all_str,type = ','.join(type))


# json format
# [common_name+op_tag,count]
io_operation = ["direct_read","direct_write","fsync"]
@app.route('/file_event_count')
def file_event_count():
    f = open(path+"/file_dio_op_counter.json")
    data = json.load(f)
    event_str = data["seq_data"].replace("[", "").replace("]", "")
    file_list = event_str.split(",") 
    data_map = {}
    for i in range(0,len(file_list),2):
        file_name,op = file_list[i].split("+")
        count = int(file_list[i+1])
        value = []
        if op == "dr":
            value = [count,0,0]
        elif op == "dw":
            value = [0,count,0]
        elif op == "fy":
            value = [0,0,count]
        else:
            value = [0,0,0]
        if file_name in data_map:
            for i in range(len(value)):
                data_map[file_name][i] += value[i] 
        else:
            data_map[file_name] = value
    category = []
    values = [[],[],[]]
    for item in data_map.items():
        category.append(item[0])
        values[0].append(item[1][0])
        values[1].append(item[1][1])
        values[2].append(item[1][2])
    value_str = [",".join(str(i) for i in value) for value in values]
    print(category)
    return render_template('func_event.html',event_name = " ".join(io_operation),event_count = " ".join(value_str),type = " ".join(category))

@app.route('/process_event_count')
def process_event_count():
    f = open(path+"/process_dio_op_counter.json")
    data = json.load(f)
    event_str = data["seq_data"].replace("[", "").replace("]", "")
    file_list = event_str.split(",") 
    data_map = {}
    for i in range(0,len(file_list),2):
        file_name,op = file_list[i].split("+")
        count = int(file_list[i+1])
        value = []
        if op == "dr":
            value = [count,0,0]
        elif op == "dw":
            value = [0,count,0]
        elif op == "fy":
            value = [0,0,count]
        else:
            value = [0,0,0]
        if file_name in data_map:
            for i in range(len(value)):
                data_map[file_name][i] += value[i] 
        else:
            data_map[file_name] = value
    category = []
    values = [[],[],[]]
    for item in data_map.items():
        category.append(item[0])
        values[0].append(item[1][0])
        values[1].append(item[1][1])
        values[2].append(item[1][2])
    value_str = [",".join(str(i) for i in value) for value in values]
    print(value_str)
    return render_template('func_event.html',event_name = " ".join(io_operation),event_count = " ".join(value_str),type = " ".join(category))

@app.route("/query_flow/<index>",methods=['GET'])
def query_flow(index):
    type = ["block_sched","nvme_execute","nvme_verify","scsi_execute","scsi_verify","account_io"]
    f = open(path+"/rq_result/"+str(index)+".json")
    data = json.load(f)
    rq_indexs = []
    latencys = [[] for i in type]
    for key in data.keys():
        if key != "dio_index":
            for i in range(len(type)):
                latencys[i].append(data[key]["latency"][i])
            rq_indexs.append(key)
    data = []
    for latency in latencys:
        latency_str = ",".join(str(i) for i in latency)
        data.append(latency_str)
    index_str = ",".join(rq_indexs)
    all_str = " ".join(data)
    print(all_str)
    return render_template('rq_latency.html',data = all_str,type = ','.join(type))


@app.route("/heatmap/<drive>/<length>",methods=['GET'])
def io_heatmap(drive,length):
    # lsblk
    f = open(path+"/"+drive+"_heatmap.json")
    data = json.load(f)
    partition = int(data["partition"])
    height = int(partition/int(length))+1
    # (length,height)
    seq = data["seq_data"]
    return render_template('heatmap.html',height = height,length = length,seq_data = seq,drive = data["name"])
#
if __name__ == "__main__":
    path = sys.argv[1].lstrip("/")
    ip = sys.argv[2]
    port = sys.argv[3]
    app.run(host=ip, port=port,debug=True)
