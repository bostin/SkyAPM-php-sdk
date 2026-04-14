import axios from 'axios'

const baseUrl = import.meta.env.BASE_URL.endsWith('/')
  ? import.meta.env.BASE_URL
  : `${import.meta.env.BASE_URL}/`

export const api = axios.create({
  baseURL: `${baseUrl}api`
})
